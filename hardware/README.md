# Hardware Directory

This directory contains hardware design files for the Flock-You ESP32
detector.

## 📁 Contents

```
hardware/
├── pcb/          ← Custom PCB design package (see pcb/README.md)
└── README.md     ← This file
```

See **[pcb/README.md](pcb/README.md)** for the PCB design brief, schematic,
BOM, and assembly guide.

---

## ⚠️ No case design is published

An earlier 3D-printable case (`CASE_DESIGN.md` in the repo root, plus
`openscad/flock-you-case.scad` and the generated `renders/`) has been
**removed from this repository**.

It was **never printed, fitted, or otherwise physically validated**, and it
was written against a breadboard build rather than the PCB. Publishing an
unvalidated model as a "professional enclosure design" invites wasted
filament and misleading build expectations, so it is gone rather than left
as a plausible-looking but unverified artifact.

**What this means for you:** there is currently no supported enclosure. The
board works bare on a desk, or you can design your own case to suit your
build. If you produce a case and physically verify the fit, contributions
are very welcome.

**Note for anyone who had the old pages bookmarked:** none of the deleted
content was depended on by firmware, the PlatformIO build, CI, or the web
flasher. Nothing in the build pipeline references it — this removal is
documentation-only.

---

## 🤝 Contributing

Improvements welcome:

- **Case design:** publish a model *only* alongside photos of a real print
  and a verified component fit — that is what makes it trustworthy
- **PCB layout:** create KiCad files from [pcb/SCHEMATIC.md](pcb/SCHEMATIC.md)
- **Testing:** report assembly issues and component substitutions
- **Documentation:** improve the guides in `pcb/`

---

## 📄 License

See [pcb/README.md](pcb/README.md) for the license covering the PCB design
package.
