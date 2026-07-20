//
// Linux symbol resolver: dladdr for the module + load base, plus a hand-parsed
// ELF .symtab for the names dladdr misses (dladdr sees only .dynsym, so static /
// anon-namespace functions come back nameless). Falls back to .dynsym, then
// module+offset, when .symtab is stripped. Native endianness; glibc ElfW() for 32/64-bit.
//

#include "SymbolResolver.h"

#include <dlfcn.h>
#include <elf.h>
#include <link.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace CXXException::detail {
    namespace {

        struct FuncSym {
            std::uintptr_t value;  // st_value
            std::uintptr_t size;   // st_size
            std::string name;      // symbol name (still mangled)
        };

        struct SymbolTable {
            bool loaded = false;
            bool is_dyn = false;              // ET_DYN -> subtract module base; ET_EXEC -> absolute
            std::vector<FuncSym> funcs;       // sorted ascending by value
        };

        // RAII read-only mmap. mmap faults in only the pages actually touched
        // (ELF header, section headers, .symtab, .strtab), so we never read the
        // whole binary — just the few sections we parse.
        struct MappedFile {
            const char *data = nullptr;
            size_t size = 0;
            explicit MappedFile(const std::string &path) {
                int fd = open(path.c_str(), O_RDONLY);
                if (fd < 0) return;
                struct stat st{};
                if (fstat(fd, &st) == 0 && st.st_size > 0) {
                    void *m = mmap(nullptr, static_cast<size_t>(st.st_size),
                                   PROT_READ, MAP_PRIVATE, fd, 0);
                    if (m != MAP_FAILED) {
                        data = static_cast<const char *>(m);
                        size = static_cast<size_t>(st.st_size);
                    }
                }
                close(fd);
            }
            ~MappedFile() { if (data) munmap(const_cast<char *>(data), size); }
            MappedFile(const MappedFile &) = delete;
            MappedFile &operator=(const MappedFile &) = delete;
        };

        SymbolTable parse_elf(const std::string &path) {
            SymbolTable table;
            // Mapping stays alive for the whole function; symbol names are copied into
            // std::string below, so the table owns them after the mapping is released.
            MappedFile file(path);
            const char *base = file.data;
            const size_t size = file.size;
            if (size < sizeof(ElfW(Ehdr))) return table;
            auto *eh = reinterpret_cast<const ElfW(Ehdr) *>(base);
            if (std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) return table;

            table.is_dyn = (eh->e_type == ET_DYN);

            if (eh->e_shoff == 0 || eh->e_shnum == 0 || eh->e_shentsize == 0) return table;
            if (eh->e_shoff + static_cast<std::uintptr_t>(eh->e_shnum) * eh->e_shentsize > size)
                return table;

            auto section = [&](unsigned i) {
                return reinterpret_cast<const ElfW(Shdr) *>(base + eh->e_shoff + i * eh->e_shentsize);
            };

            // Prefer SHT_SYMTAB (full table, includes static symbols); fall back to
            // SHT_DYNSYM (exported only) when the binary has been stripped.
            const ElfW(Shdr) *symhdr = nullptr;
            const ElfW(Shdr) *dynhdr = nullptr;
            for (unsigned i = 0; i < eh->e_shnum; ++i) {
                const ElfW(Shdr) *sh = section(i);
                if (sh->sh_type == SHT_SYMTAB) symhdr = sh;
                else if (sh->sh_type == SHT_DYNSYM) dynhdr = sh;
            }
            const ElfW(Shdr) *chosen = symhdr ? symhdr : dynhdr;
            if (!chosen || chosen->sh_entsize == 0) return table;
            if (chosen->sh_link >= eh->e_shnum) return table;

            const ElfW(Shdr) *strhdr = section(chosen->sh_link);
            if (chosen->sh_offset + chosen->sh_size > size) return table;
            if (strhdr->sh_offset + strhdr->sh_size > size) return table;

            const char *strtab = base + strhdr->sh_offset;
            const size_t strtab_size = strhdr->sh_size;
            const size_t count = chosen->sh_size / chosen->sh_entsize;

            for (size_t i = 0; i < count; ++i) {
                auto *sym = reinterpret_cast<const ElfW(Sym) *>(
                        base + chosen->sh_offset + i * chosen->sh_entsize);
                // st_info's low nibble is the type; the mask is identical for ELF32/64.
                if ((sym->st_info & 0xf) != STT_FUNC) continue;
                if (sym->st_size == 0 || sym->st_value == 0) continue;
                if (sym->st_name >= strtab_size) continue;
                const char *nm = strtab + sym->st_name;
                if (nm[0] == '\0') continue;
                table.funcs.push_back({static_cast<std::uintptr_t>(sym->st_value),
                                       static_cast<std::uintptr_t>(sym->st_size),
                                       std::string(nm)});
            }

            std::sort(table.funcs.begin(), table.funcs.end(),
                      [](const FuncSym &a, const FuncSym &b) { return a.value < b.value; });
            table.loaded = true;
            return table;
        }

        const SymbolTable &get_table(const std::string &path) {
            static std::mutex cache_mutex;
            static std::unordered_map<std::string, SymbolTable> cache;
            std::lock_guard<std::mutex> lg(cache_mutex);
            auto it = cache.find(path);
            if (it != cache.end()) return it->second;
            return cache.emplace(path, parse_elf(path)).first->second;
        }

        const char *basename_of(const char *path) {
            const char *slash = std::strrchr(path, '/');
            return slash ? slash + 1 : path;
        }

    }

    ResolvedFrame resolve_frame(void *pc) {
        ResolvedFrame rf;
        const auto addr = reinterpret_cast<std::uintptr_t>(pc);
        rf.rel_addr = addr;   // until the module (and its base) is known
        rf.func_offset = 0;
        // Return addresses: look up pc-1 (the call site), else a noreturn call at a
        // function's tail (e.g. throw) misattributes to the next function.
        const auto lookup = addr ? addr - 1 : addr;

        Dl_info info;
        if (!dladdr(reinterpret_cast<void *>(lookup), &info) || !info.dli_fname) {
            return rf;  // module unknown
        }

        rf.module = basename_of(info.dli_fname);
        const auto module_base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
        rf.rel_addr = addr - module_base;

        const SymbolTable &table = get_table(info.dli_fname);
        if (table.loaded && !table.funcs.empty()) {
            // Normalize the (call-site) address into the symbol table's coordinates.
            const std::uintptr_t key = table.is_dyn ? (lookup - module_base) : lookup;

            // Largest symbol with value <= key, then confirm containment via st_size.
            auto it = std::upper_bound(
                    table.funcs.begin(), table.funcs.end(), key,
                    [](std::uintptr_t k, const FuncSym &s) { return k < s.value; });
            if (it != table.funcs.begin()) {
                --it;
                if (key >= it->value && key < it->value + it->size) {
                    rf.function = demangle(it->name.c_str());
                    const std::uintptr_t sym_start = table.is_dyn ? module_base + it->value : it->value;
                    rf.func_offset = addr - sym_start;  // offset of the return address in the function
                    return rf;
                }
            }
        }

        // Stripped .symtab: fall back to whatever dladdr resolved from .dynsym.
        if (info.dli_sname && info.dli_sname[0]) {
            rf.function = demangle(info.dli_sname);
            if (info.dli_saddr)
                rf.func_offset = addr - reinterpret_cast<std::uintptr_t>(info.dli_saddr);
        }
        return rf;
    }

}
