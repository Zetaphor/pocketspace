---
layout: home
hero:
  name: Pocketspace
  text: Build AR in VR.
  tagline: A JavaScript platform for games, tools, toys and experiences that live alongside the SteamVR apps you already use.
  actions:
    - theme: brand
      text: Get started
      link: /getting-started
    - theme: alt
      text: Try an example
      link: /examples
features:
  - title: Games in your space
    details: Build interactive play around you with grabbable models, controller input, physics and animation—all alongside your VR apps.
    link: /examples
    linkText: Find a starting point
  - title: Tools within reach
    details: Create floating interfaces with text, images and controls, or build 3D tools you can pick up and move with either hand.
    link: /panels
    linkText: Build an interface
  - title: Toys to experiment with
    details: Stack, throw and connect objects. Add springs, motors and breakable joints to make playful mechanisms that respond to your hands.
    link: /physics-api
    linkText: Play with physics
  - title: Experiences that respond to you
    details: Bring controller input, eye gaze and finger sensor values into your creations, with personal eye-calibration support.
    link: /tracking-api
    linkText: Explore interaction
  - title: Scenes with character
    details: Animate your models, blend their movements and add PBR materials with HDR environment reflections. Make a space feel like your own.
    link: /animation-api
    linkText: Bring models to life
  - title: Creations you can return to
    details: Save and restore groups of objects and panels, including their layout, animation, physics and panel contents.
    link: /scene-persistence
    linkText: Keep your creations

---

## Your ideas, added to virtual reality.

Pocketspace brings the idea of augmented reality inside VR. Your creations form
an interactive layer in your virtual space while another SteamVR application
runs. Make a small game, a handy panel, a kinetic toy or an experience that
surrounds you.

## Start with a scene. Make it yours.

You don’t need a game-engine project to get started. Start with an example,
edit its JavaScript, and see the result in your headset. Changes reload
automatically; a failed reload keeps your current scene running.

With the [dependencies installed](getting-started.md) and SteamVR running:

```sh
cmake -S . -B build
cmake --build build -j2
./run-pocketspace.sh --scripts-dir scripts --launch grabbing.js
```

You’ll get a box you can grab with either controller. The engine targets Steam
Frame on Linux; [getting started](getting-started.md) covers setup and your first
custom scene.

## Choose what to build

Explore the [example scripts](examples.md) for ready-to-run scenes covering
interaction, UI, physics, animation, lighting and tracking. Use the
[JavaScript API](javascript-api.md) when you’re ready to combine them into your
own experience, or the [lighting guide](lighting-api.md) to add materials and
reflections.

For engine internals, see the [architecture](architecture.md). For future
capabilities, see the [roadmap](remaining-work.md).
