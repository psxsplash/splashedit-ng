#include "webexport.hh"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "player.hh"

namespace fs = std::filesystem;

namespace splash {

namespace {

std::string utf8(const fs::path& p) {
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string htmlEscaped(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}

// `s` as a double-quoted JavaScript string that is safe inside <script>:
// no quote, backslash, control character, < or > (</script>), U+2028 or
// U+2029 left as it is.
std::string jsString(const std::string& s) {
    std::string o = "\"";
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(s[i + 2]) & 0xFE) == 0xA8) {
            o += static_cast<unsigned char>(s[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
            i += 2;
        } else if (c == '"' || c == '\\' || c < 0x20 || c == '<' || c == '>' || c == 0x7F) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else {
            o += static_cast<char>(c);
        }
    }
    return o + "\"";
}

}  // namespace

std::string webDirError(const fs::path& out) { return exportDirError(out, kWebMarker, "web export"); }

std::string webPage(const std::string& title, const std::string& cue) {
    return R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<script src="enable-threads.js"></script>
<title>)html" + htmlEscaped(title) +
           R"html(</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
html, body { margin: 0; height: 100%; background: #000; overflow: hidden; }
canvas { position: fixed; inset: 0; width: 100%; height: 100%; border: 0; display: block; }
#note { position: fixed; left: 0; right: 0; top: 45%; text-align: center; color: #aaa; font: 16px sans-serif; }
</style>
</head>
<body>
<canvas id="canvas" tabindex="-1" oncontextmenu="event.preventDefault()"></canvas>
<div id="note">Loading...</div>
<script>
var cue = )html" + jsString(cue) +
           R"html(;
var note = document.getElementById('note');
function fail(why) { note.hidden = false; note.textContent = why; }
var Module = {
  canvas: document.getElementById('canvas'),
  arguments: ['-kiosk', '-run', '-iso', '/disc/' + cue],
  // The disc, fetched into the emulator's /disc before it starts.
  preRun: [function () {
    Module.addRunDependency('disc');
    (async function () {
      var get = async function (name) {
        var r = await fetch('game/' + encodeURIComponent(name));
        if (!r.ok) throw new Error(name + ': HTTP ' + r.status);
        return new Uint8Array(await r.arrayBuffer());
      };
      var text = await get(cue);
      Module.FS_createPath('/', 'disc', true, true);
      Module.FS_createDataFile('/disc', cue, text, true, true, true);
      for (var m of new TextDecoder().decode(text).matchAll(/FILE\s+"([^"]+)"/g))
        Module.FS_createDataFile('/disc', m[1], await get(m[1]), true, true, true);
      Module.removeRunDependency('disc');
    })().catch(function (e) { fail('Could not load the game: ' + e.message); });
  }],
  onRuntimeInitialized: function () { note.hidden = true; },
  print: function (t) { console.log(t); },
  printErr: function (t) { console.error(t); }
};
// Threads need the page cross-origin isolated: the server's headers, or
// enable-threads.js, which reloads the page once isolated.
if (self.crossOriginIsolated) {
  var s = document.createElement('script');
  s.src = 'pcsx-redux.js';
  document.body.appendChild(s);
} else if (!('serviceWorker' in navigator) || location.protocol === 'file:') {
  fail('This game needs to be opened from a web server, over https or from localhost.');
} else {
  setTimeout(function () {
    fail('This browser did not let the page isolate itself, which the emulator needs: try another browser, or a host that sends Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy headers.');
  }, 15000);
}
</script>
</body>
</html>
)html";
}

std::string writeWeb(const fs::path& bin, const fs::path& reduxWebDir, const fs::path& out, const std::string& title) {
    std::error_code ec;
    for (const char* f : kReduxWebFiles)
        if (!fs::is_regular_file(reduxWebDir / f, ec))
            return "the pcsx-redux web build in " + utf8(reduxWebDir) + " has no " + f;
    fs::path cue = bin;
    cue.replace_extension(".cue");
    if (!fs::is_regular_file(bin, ec) || !fs::is_regular_file(cue, ec)) return "disc image not found: " + utf8(bin);
    if (std::string e = webDirError(out); !e.empty()) return e;
    if (fs::exists(out, ec) && fs::equivalent(out, reduxWebDir, ec))
        return "the web export folder " + utf8(out) + " is the pcsx-redux web build it copies: pick another folder";
    const fs::path inBin = fs::weakly_canonical(bin, ec), inOut = fs::weakly_canonical(out, ec);
    if (std::mismatch(inOut.begin(), inOut.end(), inBin.begin(), inBin.end()).first == inOut.end())
        return "the disc image " + utf8(bin) + " is inside the web export folder " + utf8(out);
    try {
        fs::create_directories(out);
        removeExported(out, kWebMarker);
        std::string marker;
        for (const char* f : kReduxWebFiles) {
            fs::copy_file(reduxWebDir / f, out / f, fs::copy_options::overwrite_existing);
            marker += std::string(f) + "\n";
        }
        fs::create_directories(out / "game");
        fs::copy_file(bin, out / "game" / bin.filename(), fs::copy_options::overwrite_existing);
        fs::copy_file(cue, out / "game" / cue.filename(), fs::copy_options::overwrite_existing);
        const auto write = [](const fs::path& file, const std::string& text) {
            std::ofstream f(file, std::ios::binary | std::ios::trunc);
            f << text;
            f.close();
            if (!f) throw std::runtime_error("cannot write " + utf8(file));
        };
        write(out / "index.html", webPage(title, utf8(cue.filename())));
        write(out / kWebMarker, marker + "game\nindex.html\n");
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

}  // namespace splash
