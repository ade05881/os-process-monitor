# Canvas Submission Text

## GitHub URL

`https://github.com/ade05881/os-process-monitor`

## Compilation and Running Instructions

Install SDL2 dependencies on the Linux VM:

```bash
sudo apt install g++ make libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

Compile:

```bash
make
```

Run:

```bash
./bin/os_process_monitor
```

## Short Description

I built an OS Process Monitor using C++ and SDL2. The app reads Linux `/proc` data to show live process information, overall CPU usage, memory usage, and details for whichever process the user selects.

## Supported Interactions

- Click table headers to sort by PID, name, state, CPU, memory, or thread count
- Click the same header again to reverse sort order
- Scroll through the process list with the mouse wheel
- Click a process row to view more details
- Use Up/Down and Page Up/Page Down to move through the list
- Press Space to pause/resume live updates
- Press R to refresh immediately
- Press Esc to quit

## Expected Grade

47.5 / 47.5

The app includes the required visual interface, text, image rendering, solid colored rectangles/lines, timed updates, and advanced user interaction.
