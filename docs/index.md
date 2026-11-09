---
layout: home

hero:
  name: OpenOni
  text: Oni, reimplemented
  image:
    src: /emblem.svg
    alt: ""
  tagline: A from-scratch port of Bungie West's Oni (2001), function by function from the original Windows executable. One openoni.wasm for macOS, Linux, Windows and the browser. Bring your own CD.
  actions:
    - theme: brand
      text: Play in the browser
      link: /play/
      target: _self
    - theme: alt
      text: Download
      link: /guide/install
    - theme: alt
      text: What it is
      link: /guide/
    - theme: alt
      text: Status
      link: /guide/status

features:
  - title: Faithful
    details: Every subsystem is ported from Oni.exe and cites the address it came from, quirks and bugs included.
  - title: Your own copy
    details: No game data comes with OpenOni. It reads the files from your Oni CD, taken out of the installer without running it.
  - title: Built for gasm
    details: The game is a single WebAssembly module for the gasm runtime, which runs it natively on macOS, Linux and Windows, and in the browser.
---
