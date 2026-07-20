//
// macOS symbol resolver: dladdr for the module + load base, plus a hand-parsed
// Mach-O image for bounds-checked names.
//
// macOS dladdr already reads local symbols, but like any nearest-symbol lookup it
// has no upper bound, so an address in a gap (e.g. a function whose symbol was
// stripped) gets a wrong name + huge offset. We instead read LC_FUNCTION_STARTS —
// every function's boundary, which survives `strip` even when LC_SYMTAB is gutted —
// and name a frame only when a LC_SYMTAB symbol actually sits in its interval;
// otherwise we report module+offset rather than misattributing.
//
// Parses the in-memory image (from dli_fbase), so no fat-binary handling is needed.
//

#include "SymbolResolver.h"

#include <dlfcn.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace CXXException::detail {
    namespace {

        struct FuncSym {
            std::uintptr_t value;  // runtime address (n_value + slide)
            std::string name;
        };

        struct MachoTable {
            bool loaded = false;
            std::vector<std::uintptr_t> starts;  // function starts (runtime), sorted
            std::vector<FuncSym> syms;           // named symbols (runtime), sorted by value
        };

        std::uint64_t read_uleb128(const std::uint8_t *&p, const std::uint8_t *end) {
            std::uint64_t result = 0;
            int shift = 0;
            while (p < end) {
                std::uint8_t byte = *p++;
                result |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
                if (!(byte & 0x80)) break;
                shift += 7;
            }
            return result;
        }

        MachoTable parse_macho(const void *fbase) {
            MachoTable table;
            auto *header = static_cast<const mach_header_64 *>(fbase);
            if (!header || header->magic != MH_MAGIC_64) return table;
            const auto base = reinterpret_cast<std::uintptr_t>(fbase);

            std::uintptr_t text_vmaddr = 0, linkedit_vmaddr = 0, linkedit_fileoff = 0;
            const symtab_command *symtab = nullptr;
            const linkedit_data_command *func_starts = nullptr;
            bool have_text = false, have_linkedit = false;

            auto *lc = reinterpret_cast<const load_command *>(base + sizeof(mach_header_64));
            for (std::uint32_t i = 0; i < header->ncmds; ++i) {
                if (lc->cmd == LC_SEGMENT_64) {
                    auto *seg = reinterpret_cast<const segment_command_64 *>(lc);
                    if (std::strcmp(seg->segname, SEG_TEXT) == 0) {
                        text_vmaddr = seg->vmaddr;
                        have_text = true;
                    } else if (std::strcmp(seg->segname, SEG_LINKEDIT) == 0) {
                        linkedit_vmaddr = seg->vmaddr;
                        linkedit_fileoff = seg->fileoff;
                        have_linkedit = true;
                    }
                } else if (lc->cmd == LC_SYMTAB) {
                    symtab = reinterpret_cast<const symtab_command *>(lc);
                } else if (lc->cmd == LC_FUNCTION_STARTS) {
                    func_starts = reinterpret_cast<const linkedit_data_command *>(lc);
                }
                lc = reinterpret_cast<const load_command *>(
                        reinterpret_cast<std::uintptr_t>(lc) + lc->cmdsize);
            }
            if (!have_text || !have_linkedit || !func_starts) return table;

            const std::uintptr_t slide = base - text_vmaddr;
            // __LINKEDIT holds the symbol/string/function-starts data; its file
            // offsets map to memory through this base.
            auto linkedit = reinterpret_cast<const std::uint8_t *>(
                    base + linkedit_vmaddr - text_vmaddr - linkedit_fileoff);

            // Function boundaries (ULEB128 deltas cumulative from __TEXT vmaddr).
            const std::uint8_t *p = linkedit + func_starts->dataoff;
            const std::uint8_t *end = p + func_starts->datasize;
            std::uintptr_t addr = text_vmaddr;
            for (std::uint64_t delta; p < end && (delta = read_uleb128(p, end)) != 0;) {
                addr += delta;
                table.starts.push_back(addr + slide);
            }
            std::sort(table.starts.begin(), table.starts.end());

            // Named function symbols.
            if (symtab) {
                auto *syms = reinterpret_cast<const nlist_64 *>(linkedit + symtab->symoff);
                auto *strs = reinterpret_cast<const char *>(linkedit + symtab->stroff);
                for (std::uint32_t i = 0; i < symtab->nsyms; ++i) {
                    const nlist_64 &s = syms[i];
                    if (s.n_type & N_STAB) continue;              // debug stab
                    if ((s.n_type & N_TYPE) != N_SECT) continue;  // not defined in a section
                    if (s.n_value == 0 || s.n_un.n_strx == 0) continue;
                    if (s.n_un.n_strx >= symtab->strsize) continue;
                    const char *nm = strs + s.n_un.n_strx;
                    if (nm[0] == '\0') continue;
                    if (nm[0] == '_') ++nm;  // strip the C symbol underscore
                    table.syms.push_back({static_cast<std::uintptr_t>(s.n_value) + slide,
                                          std::string(nm)});
                }
                std::sort(table.syms.begin(), table.syms.end(),
                          [](const FuncSym &a, const FuncSym &b) { return a.value < b.value; });
            }

            table.loaded = !table.starts.empty();
            return table;
        }

        const MachoTable &get_table(const char *path, const void *fbase) {
            static std::mutex cache_mutex;
            static std::unordered_map<std::string, MachoTable> cache;
            std::lock_guard<std::mutex> lg(cache_mutex);
            auto it = cache.find(path);
            if (it != cache.end()) return it->second;
            return cache.emplace(path, parse_macho(fbase)).first->second;
        }

        const char *basename_of(const char *path) {
            const char *slash = std::strrchr(path, '/');
            return slash ? slash + 1 : path;
        }
    }

    ResolvedFrame resolve_frame(void *pc) {
        ResolvedFrame rf;
        const auto addr = reinterpret_cast<std::uintptr_t>(pc);
        rf.rel_addr = addr;
        rf.func_offset = 0;
        // Return addresses: resolve pc-1 (the call site), else a noreturn call at a
        // function's tail (e.g. throw) misattributes to the next symbol.
        const auto lookup = addr ? addr - 1 : addr;

        Dl_info info;
        if (!dladdr(reinterpret_cast<void *>(lookup), &info) || !info.dli_fname) {
            return rf;
        }

        rf.module = basename_of(info.dli_fname);
        rf.rel_addr = addr - reinterpret_cast<std::uintptr_t>(info.dli_fbase);

        const MachoTable &table = get_table(info.dli_fname, info.dli_fbase);
        if (table.loaded) {
            // Which function does the call site fall in?
            auto sit = std::upper_bound(table.starts.begin(), table.starts.end(), lookup);
            if (sit != table.starts.begin()) {
                const std::uintptr_t fstart = *(sit - 1);
                const std::uintptr_t fend =
                        (sit == table.starts.end()) ? UINTPTR_MAX : *sit;
                // Name it only if a symbol sits inside this function's interval;
                // otherwise it is a real-but-stripped function -> module+offset.
                auto yit = std::upper_bound(table.syms.begin(), table.syms.end(), lookup,
                                            [](std::uintptr_t k, const FuncSym &s) { return k < s.value; });
                if (yit != table.syms.begin() && (yit - 1)->value >= fstart && (yit - 1)->value < fend) {
                    rf.function = demangle((yit - 1)->name.c_str());
                }
                rf.func_offset = addr - fstart;
                return rf;
            }
        }

        // Parse failed or before the first function: fall back to dladdr's symbol.
        if (info.dli_sname && info.dli_sname[0]) {
            rf.function = demangle(info.dli_sname);
            if (info.dli_saddr)
                rf.func_offset = addr - reinterpret_cast<std::uintptr_t>(info.dli_saddr);
        }
        return rf;
    }
}
