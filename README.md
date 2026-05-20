# OS Process Monitor

An interactive SDL2 task-manager style application that visualizes live Linux process data from `/proc`.

The app shows:

- Overall CPU usage
- Memory usage
- A sortable, scrollable process table
- A selected-process command display
- A loaded chip image asset, rendered text, solid rectangles, bars, and separator lines

## Install Dependencies

On the Linux VM:

```bash
sudo apt install g++ make libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

SDL reference: <https://www.libsdl.org>

## Compile

```bash
make
```

## Run

```bash
./bin/os_process_monitor
```

or:

```bash
make run
```

The program refreshes live OS data about every 500 ms. If it is run on a machine without `/proc`, it displays demo rows so the interface can still be viewed.

## Interactions

- Click a table header to sort by PID, process name, state, CPU usage, memory usage, or thread count
- Click the same header again to reverse the sort direction
- Use the mouse wheel to scroll through processes
- Click a process row to show details
- Use Up/Down and Page Up/Page Down to move through the table
- Press Space to pause or resume live refresh
- Press R to refresh immediately
- Press Esc to quit

## Project Description

I built an OS Process Monitor. It reads process information from Linux `/proc`, estimates per-process CPU usage between refreshes, displays system CPU and memory metrics, and lets the user inspect and sort currently running processes.

## Expected Grade

47.5 / 47.5

This project includes the basic visual interface requirements and the advanced interaction requirements: live updates, text, an image, colored rectangles/lines, sorting, scrolling, row selection, and keyboard controls.
