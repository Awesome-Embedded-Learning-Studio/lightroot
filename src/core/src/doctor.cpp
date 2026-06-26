#include <lightroot/doctor.h>

#include <lightroot/exec.h>
#include <lightroot/scope_guard.h>

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <unistd.h>

namespace lightroot {
namespace {

/// ELF machine type for 64-bit AArch64 (e_machine).
constexpr unsigned EM_ARM = 0x28;     ///< 32-bit ARM (e_machine).
constexpr unsigned EM_AARCH64 = 0xB7; ///< 64-bit ARM (e_machine).

/// Join `parts` with `sep`.
std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += sep;
        out += parts[i];
    }
    return out;
}

/// Strip leading/trailing ASCII whitespace in place.
void trim(std::string& s) {
    constexpr const char* WS = " \t\r\n\v\f";
    const auto first = s.find_first_not_of(WS);
    if (first == std::string::npos) {
        s.clear();
        return;
    }
    const auto last = s.find_last_not_of(WS);
    s = s.substr(first, last - first + 1);
}

/// Whether `p` is a regular file the current user may execute.
bool isExecutable(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec))
        return false;
    return ::access(p.c_str(), X_OK) == 0;
}

/// Search PATH for an executable named `name` (delegates to the exec primitive).
std::optional<std::filesystem::path> lookupOnPath(std::string_view name) {
    return findOnPath(name);
}

/// Parse the ELF e_machine field (returns nullopt if not a recognizable ELF).
std::optional<unsigned> elfMachine(const std::string& bytes) {
    if (bytes.size() < 20 || bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' ||
        bytes[3] != 'F')
        return std::nullopt;
    const bool little = static_cast<unsigned char>(bytes[5]) == 1; // EI_DATA: 1 = LSB
    const auto b0 = static_cast<unsigned char>(bytes[18]);
    const auto b1 = static_cast<unsigned char>(bytes[19]);
    return little ? static_cast<unsigned>(b0 | (b1 << 8)) : static_cast<unsigned>((b0 << 8) | b1);
}

/// Create a fresh temp directory tagged `tag`; caller removes it via ScopeGuard.
std::filesystem::path makeTempDir(std::string_view tag) {
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec) /
               ("lightroot-" + std::string(tag) + "-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir, ec);
    return dir;
}

/// Overwrite `path` with `body`.
void writeFile(const std::filesystem::path& path, std::string_view body) {
    std::ofstream of(path);
    of << body;
}

/// Run `{bin} --static -o out src [--sysroot=...]`, capturing combined output.
CommandResult runStaticCompile(const std::filesystem::path& bin, const ToolchainConfig& tc,
                               const std::filesystem::path& src, const std::filesystem::path& out,
                               std::string& cap) {
    std::vector<std::string> argv{bin.string(), "--static", "-o", out.string(), src.string()};
    if (!tc.sysroot.empty())
        argv.push_back("--sysroot=" + tc.sysroot);
    return runCapture(argv, {}, cap);
}

/// Build a failure detail: `what failed (exit N)[: trimmed snippet]`.
std::string failDetail(std::string_view what, const CommandResult& res, const std::string& cap) {
    std::string d = std::string(what) + " failed (exit " + std::to_string(res.exit_code) + ")";
    std::string t = cap;
    trim(t);
    if (!t.empty())
        d += ": " + t.substr(0, std::min<std::size_t>(t.size(), 200));
    return d;
}

/// Expected ELF e_machine for a target triple (arm* -> EM_ARM, aarch64* ->
/// EM_AARCH64). Returns 0 for an unknown family, in which case applyArchVerdict
/// accepts any valid ELF (the smoke check stays useful without an arch guess).
unsigned expectedElfMachine(std::string_view target) {
    if (target.rfind("aarch64", 0) == 0)
        return EM_AARCH64;
    if (target.rfind("arm", 0) == 0)
        return EM_ARM;
    return 0;
}

/// Set status/detail from the ELF machine type of `elf`; Pass with @p ok_detail
/// when it matches @p expected (or when @p expected is 0 / unknown). The host
/// toolchain's own target triple drives @p expected, so the smoke check is
/// arch-agnostic rather than hard-wired to aarch64.
void applyArchVerdict(CheckResult& r, const std::filesystem::path& elf, unsigned expected,
                      std::string_view ok_detail) {
    const auto machine = elfMachine(readFile(elf));
    if (!machine) {
        r.status = CheckStatus::Warn;
        r.detail = "compiled, but output is not a valid ELF";
        return;
    }
    if (expected == 0 || *machine == expected) {
        r.status = CheckStatus::Pass;
        r.detail = std::string(ok_detail);
        return;
    }
    r.status = CheckStatus::Warn;
    char got[16], want[16];
    std::snprintf(got, sizeof(got), "0x%04x", *machine);
    std::snprintf(want, sizeof(want), "0x%04x", expected);
    r.detail = std::string{"compiled, but e_machine="} + got + " (target expects " + want + ")";
}

} // namespace

// NOLINTNEXTLINE
const char* to_string(CheckStatus s) {
    switch (s) {
        case CheckStatus::Pass:
            return "PASS";
        case CheckStatus::Warn:
            return "WARN";
        case CheckStatus::Fail:
            return "FAIL";
    }
    return "FAIL";
}

bool DoctorReport::ok() const {
    for (const auto& c : checks)
        if (c.status == CheckStatus::Fail)
            return false;
    return true;
}

std::optional<std::filesystem::path> findTool(const ToolchainConfig& tc, std::string_view tool) {
    const std::string name = tc.prefix + std::string(tool);

    if (!tc.path.empty()) {
        const auto candidate = std::filesystem::path(tc.path) / name;
        if (isExecutable(candidate))
            return candidate;
    }
    return lookupOnPath(name);
}

CheckResult checkTools(const ToolchainConfig& tc) {
    static constexpr std::string_view TOOLS[] = {"gcc", "g++", "ar", "strip", "ld"};

    CheckResult r;
    r.name = "tools";

    std::vector<std::string> missing;
    for (auto t : TOOLS) {
        if (!findTool(tc, t))
            missing.push_back(tc.prefix + std::string(t));
    }

    std::string version;
    if (const auto gcc = findTool(tc, "gcc")) {
        std::string out;
        runCapture({gcc->string(), "--version"}, {}, out);
        const auto nl = out.find('\n');
        version = nl == std::string::npos ? out : out.substr(0, nl);
        trim(version);
    }

    if (!missing.empty()) {
        r.status = CheckStatus::Fail;
        r.detail = "missing: " + join(missing, ", ");
    } else {
        r.status = CheckStatus::Pass;
        r.detail = version.empty() ? std::string{"all core tools found"} : version;
    }
    return r;
}

CheckResult checkTarget(const ToolchainConfig& tc) {
    CheckResult r;
    r.name = "target";

    if (tc.target.empty()) {
        r.status = CheckStatus::Warn;
        r.detail = "target not specified in config";
        return r;
    }

    const auto gcc = findTool(tc, "gcc");
    if (!gcc) {
        r.status = CheckStatus::Fail;
        r.detail = "compiler not found";
        return r;
    }

    std::string out;
    if (!runCapture({gcc->string(), "-dumpmachine"}, {}, out).ok()) {
        r.status = CheckStatus::Fail;
        r.detail = "gcc -dumpmachine failed";
        return r;
    }
    trim(out);

    if (out == tc.target) {
        r.status = CheckStatus::Pass;
        r.detail = out;
    } else {
        r.status = CheckStatus::Fail;
        r.detail = "gcc reports '" + out + "', config wants '" + tc.target + "'";
    }
    return r;
}

CheckResult checkSysroot(const ToolchainConfig& tc) {
    CheckResult r;
    r.name = "sysroot";

    if (!tc.sysroot.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(tc.sysroot, ec)) {
            r.status = CheckStatus::Pass;
            r.detail = tc.sysroot;
        } else {
            r.status = CheckStatus::Fail;
            r.detail = "not a directory: " + tc.sysroot;
        }
        return r;
    }

    // Unspecified: report the compiler's default, but warn (config should be explicit).
    const auto gcc = findTool(tc, "gcc");
    std::string def;
    if (gcc) {
        runCapture({gcc->string(), "--print-sysroot"}, {}, def);
        trim(def);
    }
    r.status = CheckStatus::Warn;
    if (def.empty())
        r.detail = "sysroot not specified; compiler default unknown";
    else
        r.detail = "sysroot not specified; compiler default is '" + def + "'";
    return r;
}

CheckResult checkCompileSmoke(const ToolchainConfig& tc) {
    CheckResult r;
    r.name = "compile-smoke";

    const auto gcc = findTool(tc, "gcc");
    if (!gcc) {
        r.status = CheckStatus::Fail;
        r.detail = "gcc not found";
        return r;
    }

    const auto tmp = makeTempDir("compile-smoke");
    ScopeGuard cleanup([&] {
        std::error_code e;
        std::filesystem::remove_all(tmp, e);
    });
    const auto src = tmp / "smoke.c";
    const auto out = tmp / "smoke";
    writeFile(src, "#include <stdio.h>\nint main(void){ (void)puts(\"ok\"); return 0; }\n");

    std::string cap;
    const auto res = runStaticCompile(*gcc, tc, src, out, cap);
    if (!res.ok()) {
        r.status = CheckStatus::Fail;
        r.detail = failDetail("static compile", res, cap);
        return r;
    }

    applyArchVerdict(r, out, expectedElfMachine(tc.target), "static binary produced");
    return r;
}

CheckResult checkCppSmoke(const ToolchainConfig& tc) {
    CheckResult r;
    r.name = "c++-smoke";

    const auto gpp = findTool(tc, "g++");
    if (!gpp) {
        r.status = CheckStatus::Fail;
        r.detail = "g++ not found";
        return r;
    }

    const auto tmp = makeTempDir("cpp-smoke");
    ScopeGuard cleanup([&] {
        std::error_code e;
        std::filesystem::remove_all(tmp, e);
    });
    const auto src = tmp / "smoke.cpp";
    const auto out = tmp / "smoke";
    writeFile(src, "#include <cstdio>\n#include <string>\n"
                   "int main(){ std::string s = \"ok\"; std::puts(s.c_str()); return "
                   "(int)s.size() - 2; }\n");

    std::string cap;
    const auto res = runStaticCompile(*gpp, tc, src, out, cap);
    if (!res.ok()) {
        r.status = CheckStatus::Fail;
        r.detail = failDetail("static C++ compile", res, cap);
        return r;
    }

    applyArchVerdict(r, out, expectedElfMachine(tc.target), "static C++ binary produced");
    return r;
}

CheckResult checkStrip(const ToolchainConfig& tc) {
    CheckResult r;
    r.name = "strip";

    const auto gcc = findTool(tc, "gcc");
    const auto strip = findTool(tc, "strip");
    if (!gcc) {
        r.status = CheckStatus::Fail;
        r.detail = "gcc not found";
        return r;
    }
    if (!strip) {
        r.status = CheckStatus::Fail;
        r.detail = "strip not found";
        return r;
    }

    const auto tmp = makeTempDir("strip");
    ScopeGuard cleanup([&] {
        std::error_code e;
        std::filesystem::remove_all(tmp, e);
    });
    const auto src = tmp / "s.c";
    const auto bin = tmp / "s";
    writeFile(src, "int main(void){return 0;}\n");

    std::string cap;
    const auto cres = runStaticCompile(*gcc, tc, src, bin, cap);
    if (!cres.ok()) {
        r.status = CheckStatus::Fail;
        r.detail = failDetail("preparing binary", cres, cap);
        return r;
    }

    std::error_code ec;
    const auto before = std::filesystem::file_size(bin, ec);
    std::string scap;
    const auto sres = runCapture({strip->string(), bin.string()}, {}, scap);
    if (!sres.ok()) {
        r.status = CheckStatus::Fail;
        r.detail = failDetail("strip", sres, scap);
        return r;
    }

    const auto after = std::filesystem::file_size(bin, ec);
    if (ec || after >= before) {
        r.status = CheckStatus::Warn;
        r.detail = "strip ran but did not reduce the binary";
    } else {
        r.status = CheckStatus::Pass;
        r.detail = "stripped " + std::to_string(before) + " -> " + std::to_string(after) + " bytes";
    }
    return r;
}

CheckResult checkHostTools(const DoctorOptions& opts) {
    CheckResult r;
    r.name = "host-tools";

    std::vector<std::string> missing;
    for (const auto& t : opts.hostTools)
        if (!lookupOnPath(t))
            missing.push_back(t);

    const bool fetch = lookupOnPath("curl").has_value() || lookupOnPath("wget").has_value();
    const bool qemu = lookupOnPath("qemu-aarch64").has_value();
    const bool fakeroot = lookupOnPath("fakeroot").has_value();

    std::vector<std::string> notes;
    if (!missing.empty())
        notes.push_back("missing: " + join(missing, ", "));
    if (!fetch)
        notes.push_back("no fetch tool (curl/wget)");
    if (!qemu)
        notes.push_back("qemu-aarch64 absent (only needed for Stage 4)");
    if (!fakeroot)
        notes.push_back("fakeroot absent (rootfs will lack device nodes + root ownership)");

    if (!missing.empty())
        r.status = CheckStatus::Fail;
    else if (!fetch || !qemu || !fakeroot)
        r.status = CheckStatus::Warn;
    else
        r.status = CheckStatus::Pass;

    r.detail = notes.empty() ? std::string{"all host tools present"} : join(notes, "; ");
    return r;
}

DoctorReport runDoctor(const ToolchainConfig& tc, const DoctorOptions& opts) {
    DoctorReport rep;
    rep.checks.push_back(checkTools(tc));
    rep.checks.push_back(checkTarget(tc));
    rep.checks.push_back(checkSysroot(tc));
    if (!opts.skipSmoke) {
        rep.checks.push_back(checkCompileSmoke(tc));
        rep.checks.push_back(checkCppSmoke(tc));
        rep.checks.push_back(checkStrip(tc));
    }
    rep.checks.push_back(checkHostTools(opts));
    return rep;
}

} // namespace lightroot
