#!/usr/bin/env python3
"""Generate the print-ready quick-start guides as PDFs.

Outputs, next to this script:
    quick-start-4x6.pdf     4 x 6 in  (pocket card, prints double-sided)
    quick-start-letter.pdf  8.5 x 11 in (US Letter sheet)

Why Chrome and not a Python PDF library: `--print-to-pdf` needs no
dependency to install, and it renders the same CSS a user gets from the
browser's own Print dialog, so the on-screen layout is a faithful preview
of the printed page.

The guide text lives once, in CONTENT below, and is rendered twice with the
page geometry and base font size swapped.  That is deliberate: two hand-
written copies of the same instructions drift apart, and the copy nobody
rereads is the one that ends up wrong.

Each page carries a hidden self-measurement (the <script> at the bottom)
that reports its own content height vs. available height.  Run:

    python3 make_quick_start_pdfs.py --check

to print that measurement without writing any files -- it is how you catch
text silently clipped by the fixed page height after an edit.
"""

import json
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CHROME_CANDIDATES = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
]

# Page geometry per target.  `base` is the root font size in points; every
# other size in the CSS is in em, so the whole card scales from this one
# number while the layout stays identical between the two prints.
SIZES = {
    "4x6": {
        "page": "4in 6in", "w": "4in", "h": "6in",
        "base": "8.3pt", "pad": "0.17in 0.19in",
    },
    "letter": {
        "page": "8.5in 11in", "w": "8.5in", "h": "11in",
        "base": "13pt", "pad": "0.7in 0.8in",
    },
}
# Print-first CSS.  No emoji anywhere in the body text: colour-emoji glyphs
# embed poorly in PDF and vary by machine, and a guide that prints as boxes is
# worse than one with no icons at all.
CSS = """
@page { size: __PAGE__; margin: 0; }
* { box-sizing: border-box; }
html, body { margin: 0; padding: 0; }
body {
  font-family: -apple-system, "Helvetica Neue", Helvetica, Arial, sans-serif;
  color: #14181c; font-size: __BASE__; line-height: 1.27;
  -webkit-print-color-adjust: exact; print-color-adjust: exact;
}
.page {
  width: __W__; height: __H__; padding: __PAD__; overflow: hidden;
  page-break-after: always; display: flex; flex-direction: column;
}
.page.last { page-break-after: auto; }
.kicker { font-size: .72em; font-weight: 700; letter-spacing: .12em;
          text-transform: uppercase; color: #6b7480; }
h1 { font-size: 1.74em; line-height: 1.08; margin: .1em 0 .14em; letter-spacing: -.015em; }
.tag { margin: 0; font-size: .97em; color: #3d4650; }
.rule { height: 3px; background: #1f7a6b; margin: .52em 0 .68em; border-radius: 2px; }
.card { border: 1px solid #d9dee4; border-radius: 5px; padding: .44em .6em;
        margin: 0 0 .42em; background: #fafbfc; }
h2 { font-size: .74em; font-weight: 700; letter-spacing: .1em;
     text-transform: uppercase; color: #1f7a6b; margin: 0 0 .28em; }
.card p { margin: 0 0 .3em; }
.card p:last-child { margin-bottom: 0; }
ul, ol { margin: 0; padding-left: 1.05em; }
li { margin: 0 0 .16em; }
li:last-child { margin-bottom: 0; }
table.key { width: 100%; border-collapse: collapse; }
table.key td { padding: .14em 0; vertical-align: top; border-bottom: 1px solid #e7ebef; }
table.key tr:last-child td { border-bottom: 0; }
table.key td.k { font-weight: 600; padding-right: .6em; white-space: nowrap; }
table.key td.k::before { content: "\\2022  "; color: #1f7a6b; }
.callout { background: #eef6f4; border: 1px solid #bcd9d3; border-radius: 5px;
           padding: .42em .58em; margin: 0 0 .48em; font-weight: 600; color: #14544a; }
.foot { margin-top: auto; padding-top: .34em; font-size: .74em; color: #6b7480;
        border-top: 1px solid #d9dee4; }
"""

SHELL = """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<title>__TITLE__</title>
<style>__CSS__</style></head>
<body>
__BODY__
<div id="measure" style="display:none"></div>
<script>
(function () {
  // Measuring pass only (see measure()): with overflow:hidden, scrollHeight
  // clamps to the box height for content that FITS, so a page that is nearly
  // full and one that is comfortably empty both report "0 slack".  Temporarily
  // releasing the height gives the true content height and therefore real
  // headroom.  Gated on a query string so the PDF pass, which loads this file
  // without it, can never be measured mid-reflow.
  var measuring = location.search.indexOf('measure=1') !== -1;
  var out = [];
  document.querySelectorAll('.page').forEach(function (p, i) {
    var cs = getComputedStyle(p);
    var pad = parseFloat(cs.paddingTop) + parseFloat(cs.paddingBottom);
    var avail = p.clientHeight - pad;
    if (measuring) { p.style.height = 'auto'; p.style.overflow = 'visible'; }
    var used = p.getBoundingClientRect().height - pad;
    out.push({page: i + 1, avail: Math.round(avail), used: Math.round(used),
              slack: Math.round(avail - used)});
  });
  document.getElementById('measure').textContent = 'MEASURE=' + JSON.stringify(out);
})();
</script>
</body></html>
"""
# The guide itself -- one source, rendered at both sizes.
BODY = """
<div class="page">
  <div class="kicker">Pocket Guide</div>
  <h1>Flock-You Detector</h1>
  <p class="tag">A small device that beeps when a license-plate camera is near.</p>
  <div class="rule"></div>

  <div class="card">
    <h2>What it does</h2>
    <p>It listens for the quiet radio signals that surveillance cameras give off.
       When one is close, it beeps and the light on the board flashes.</p>
    <p class="callout">It never sends anything, never connects to the internet,
       and never records anyone.</p>
  </div>

  <div class="card">
    <h2>What it finds</h2>
    <ul>
      <li>License-plate reading cameras, the box-on-a-pole kind</li>
      <li>The battery packs and accessories that go with them</li>
      <li>Some other camera equipment that shares the same radio</li>
    </ul>
  </div>

  <div class="card">
    <h2>Before you start</h2>
    <p>You need the detector, a USB cable, and power &mdash; a phone charger,
       a laptop, or a USB battery pack if you want to carry it around.</p>
  </div>

  <div class="card">
    <h2>Turn it on</h2>
    <ol>
      <li>Plug the cable into the detector and into power.</li>
      <li>Wait a few seconds. A short six-note tune plays. That means it is ready.</li>
      <li>No tune on your version? Watch the small light instead.</li>
    </ol>
  </div>

  <div class="card">
    <h2>What you will hear and see</h2>
    <table class="key">
      <tr><td class="k">Six-note tune, once</td><td>Switched on and working</td></tr>
      <tr><td class="k">Two rising beeps</td><td>A camera is close by, right now</td></tr>
      <tr><td class="k">Light flashes</td><td>The same thing. Works with no sound</td></tr>
      <tr><td class="k">Silence</td><td>Normal. Nothing close by</td></tr>
    </table>
  </div>

  <div class="foot">Turn over for how to use it &rarr;</div>
</div>

<div class="page last">
  <div class="card">
    <h2>How to use it</h2>
    <ol>
      <li><b>Carry it with power.</b> Battery pack, then a bag, bike or dashboard.</li>
      <li><b>Go about your normal trip.</b> Walk, ride or drive as usual.</li>
      <li><b>When it beeps, slow down.</b> A camera is very close. Look around.</li>
      <li><b>Same beep in the same place twice?</b> You have found one.</li>
      <li><b>Press the button</b> to save what it found. One short beep confirms.</li>
    </ol>
  </div>

  <div class="card">
    <h2>Good to know</h2>
    <ul>
      <li>It only hears what is close: a few hundred feet outdoors, much less
          indoors or in traffic.</li>
      <li>Silence means nothing is close right now, not that there are no cameras
          anywhere.</li>
      <li>It will not beep twice for the same camera &mdash; after the first alert it
          goes quiet for a while.</li>
      <li>Weak signals are recorded silently, so a beep means a strong match. One
          beep is worth a look, not proof.</li>
    </ul>
  </div>

  <div class="card">
    <h2>If something seems wrong</h2>
    <table class="key">
      <tr><td class="k">No sound at all</td><td>Your version may have no buzzer. Watch the light.</td></tr>
      <tr><td class="k">Nothing happens</td><td>Try another USB cable and another charger.</td></tr>
      <tr><td class="k">Beeps non-stop</td><td>That is a real, strong signal. Move away and it stops.</td></tr>
    </table>
  </div>

  <div class="card">
    <h2>Use it responsibly</h2>
    <ul>
      <li>It only listens to radio signals already in the air. It never transmits,
          never joins a network, and never records anyone.</li>
      <li>Never interfere with any camera or equipment.</li>
      <li>Follow local laws, stay on public ground, and do not trespass.</li>
    </ul>
  </div>

  <div class="foot">github.com/simeononsecurity/flock-you-esp32</div>
</div>
"""
TITLE = "Flock-You Detector - How to Use It"


def render_html(size_key):
    """Render the shared guide at one page size."""
    s = SIZES[size_key]
    css = (CSS
           .replace("__PAGE__", s["page"])
           .replace("__W__", s["w"])
           .replace("__H__", s["h"])
           .replace("__BASE__", s["base"])
           .replace("__PAD__", s["pad"]))
    return (SHELL
            .replace("__TITLE__", TITLE)
            .replace("__CSS__", css)
            .replace("__BODY__", BODY))


def find_chrome():
    for c in CHROME_CANDIDATES:
        if pathlib.Path(c).exists():
            return c
    return None


def chrome_run(chrome, args):
    cmd = [chrome, "--headless", "--disable-gpu", "--no-sandbox",
           "--virtual-time-budget=4000"] + args
    return subprocess.run(cmd, capture_output=True, text=True)


def measure(chrome, html_path):
    """Ask the page how tall its content is, so clipping cannot go unnoticed.

    overflow:hidden on .page means an over-long section is cut off silently in
    the PDF -- no error, no warning, just missing text.  The in-page script
    writes clientHeight vs scrollHeight into a hidden div; --dump-dom then
    hands us the numbers.
    """
    res = chrome_run(chrome, ["--dump-dom", html_path.as_uri() + "?measure=1"])
    m = re.search(r"MEASURE=(\[[^\]]*\])", res.stdout)
    if not m:
        return None
    return json.loads(m.group(1).replace("&quot;", '"'))


def make_pdf(chrome, html_path, pdf_path):
    res = chrome_run(chrome, ["--no-pdf-header-footer",
                              "--print-to-pdf=" + str(pdf_path),
                              html_path.as_uri()])
    if not pdf_path.exists():
        sys.stderr.write(res.stderr[-2000:])
        raise SystemExit("FAILED to produce " + pdf_path.name)
    return pdf_path.stat().st_size


def pdf_geometry(pdf_path):
    """Best-effort read of page count/size straight from the PDF bytes."""
    raw = pdf_path.read_bytes()
    boxes = re.findall(rb"/MediaBox\s*\[\s*0\s+0\s+([0-9.]+)\s+([0-9.]+)\s*\]", raw)
    pages = len(re.findall(rb"/Type\s*/Page[^s]", raw))
    if not boxes:
        return pages, None
    w, h = boxes[0]
    return pages, (round(float(w) / 72.0, 2), round(float(h) / 72.0, 2))


def main():
    check_only = "--check" in sys.argv
    chrome = find_chrome()
    if not chrome:
        raise SystemExit("No Chrome/Chromium found; install one or add its path.")

    ok = True
    for key in SIZES:
        html_path = HERE / ("_build-%s.html" % key)
        html_path.write_text(render_html(key), encoding="utf-8")
        try:
            stats = measure(chrome, html_path)
            if stats is None:
                ok = False
                print("  %-7s measurement unavailable" % key)
            else:
                for s in stats:
                    flag = "OK  " if s["slack"] >= 0 else "CLIP"
                    if s["slack"] < 0:
                        ok = False
                    print("  %-7s page %d: %4dpt used of %4dpt avail  slack %+4d  %s"
                          % (key, s["page"], s["used"], s["avail"], s["slack"], flag))
            if not check_only:
                pdf_path = HERE / ("quick-start-%s.pdf" % key)
                size = make_pdf(chrome, html_path, pdf_path)
                pages, dims = pdf_geometry(pdf_path)
                dim_txt = ("%sx%s in" % dims) if dims else "size unread"
                print("  %-7s wrote %s  pages=%s  %s  %d bytes"
                      % (key, pdf_path.name, pages, dim_txt, size))
        finally:
            html_path.unlink(missing_ok=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
