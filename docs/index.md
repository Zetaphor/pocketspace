---
layout: home
hero:
  name: Pocketspace
  text: Build interactive tools for VR.
  tagline: Create workspaces, control panels and hands-on scenes in JavaScript. Run them alongside the SteamVR apps you already use.
  actions:
    - theme: brand
      text: Get started
      link: /getting-started
    - theme: alt
      text: Try an example
      link: /examples
features:
  - title: Objects you can grab
    details: Bring GLB models into your space. Pick them up, rotate them and interact with either controller using grip, trigger or both.
    link: /javascript-api
    linkText: Explore objects and interaction
  - title: Panels you can interact with
    details: Build floating interfaces with text, images and controls. Respond to controller pointer clicks to make your own VR tools.
    link: /panels
    linkText: Build a panel
  - title: Physics and mechanisms
    details: Add gravity, collisions, springs, motors and breakable joints. Turn a collection of objects into something you can move and experiment with.
    link: /physics-api
    linkText: Add physics
  - title: Animation and materials
    details: Play and blend model animations, give each object independent playback, and add PBR lighting and HDR environment reflections.
    link: /animation-api
    linkText: Animate your scene
  - title: Workspaces you can save
    details: Keep related objects and panels together. Save and restore their layout, animation, physics and panel contents.
    link: /scene-persistence
    linkText: Save a workspace
  - title: Tracking-aware tools
    details: React to controller input, eye gaze and finger sensor values. Apply personal eye calibration to suit your headset fit.
    link: /tracking-api
    linkText: Explore tracking
---

## Start with a scene. Make it yours.

You don’t need a game-engine project to build a tool. Start with an example,
edit its JavaScript, and see the result in your headset. Changes reload
automatically; a failed reload keeps your current scene running.

With the [dependencies installed](getting-started.md) and SteamVR running:

```sh
cmake -S . -B build
cmake --build build -j2
./run-pocketspace.sh scripts/grabbing.js
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
