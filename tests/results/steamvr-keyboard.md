# SteamVR keyboard for overlay text input

Whether an overlay application can use SteamVR's keyboard for text input on Steam
Frame, for the [builder plan](../../docs/builder-plan.md).

## Method

A standalone OpenVR program, not part of the engine or its targets, initialized
as `VRApplication_Overlay` like the engine. It created a 0.3 m overlay 0.6 m in
front of the head, then called `ShowKeyboardForOverlay` with single-line normal
input, a 64-character limit, existing text `Thing 1` and user value 42. It
polled that overlay's event queue and the system event queue every 11 ms, and
called `GetKeyboardText` after the keyboard closed. SteamVR and a foreground app
were running; the engine was not. A physical Bluetooth keyboard was connected.

## Results

| Run | Flags | Typed | Received on the overlay queue |
| --- | --- | --- | --- |
| 1 | Minimal | Letters on the VR keyboard, then Done | One `KeyboardCharInput` per key, then `KeyboardDone`, `KeyboardClosed` and `KeyboardClosed_Global` together |
| 2 | Minimal | `abc` on the physical keyboard, `xyz` on the VR keyboard, Done | `x`, `y`, `z` only, then the close events |
| 3 | Minimal, show arrow keys | `ab`, Backspace, ←, →, Done | `a`, `b`, `0x08`, `ESC [ D`, `ESC [ C`, then the close events |

- `ShowKeyboardForOverlay` succeeded for the overlay application.
  `KeyboardOpened_Global` arrived on both queues with the overlay's handle.
- Keys arrive on the overlay's own queue with the request's user value.
  `KeyboardDone` reports overlay handle 0 and the user value.
- In minimal mode the existing text is not shown and `GetKeyboardText` returns
  only the last key, so the application must keep the text, including the
  initial value.
- Backspace arrives as byte `0x08`. Left and right arrows arrive as the ANSI
  sequences `ESC [ D` and `ESC [ C`.
- Physical keyboard keys do not reach the SteamVR keyboard; they go to the
  focused desktop application.
- The user reported that the keyboard did not interfere with the foreground
  application.
- The program exited cleanly after every run.

## Conclusion

Use the SteamVR keyboard in minimal mode, with the engine holding the text and
applying backspace and arrow keys. Physical keyboard input is not supported.
