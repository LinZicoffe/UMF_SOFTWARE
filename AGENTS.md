# Repository Guidelines

## Project Structure & Module Organization

This repository contains STM32 firmware and a Windows host application. Firmware entry points and CubeMX-generated files live in `Core/Inc` and `Core/Src`; keep generated edits inside `/* USER CODE BEGIN/END */` blocks. Board-level features (Modbus, keys, menus, calibration, and Flash storage) belong in `BSP/`. Display drivers and font assets are in `OLED/` and `newOLED/`, while vendor HAL/CMSIS code is under `Drivers/`. The IAR workspace is `EWARM/Project.eww`. The .NET 8 WPF application is in `HostApplication/UMF流量计/`, organized into `Views`, `ViewModels`, `Models`, `Services`, and `Communication`. Protocol and design decisions are documented in root-level Markdown files and `docs/`.

## Build, Test, and Development Commands

- Open `EWARM/Project.eww` in IAR EWARM 8.32 and run **Project → Make (F7)** to build firmware. There is no Makefile or CMake configuration; add every new `.c` file to `EWARM/UMF.ewp`.
- `dotnet restore "HostApplication/UMF流量计.sln"` restores WPF dependencies.
- `dotnet build "HostApplication/UMF流量计.sln"` compiles the desktop application.
- `dotnet run --project "HostApplication/UMF流量计/UMF流量计.csproj"` launches the host application on Windows.

## Coding Style & Naming Conventions

Firmware is C99-compatible C with four-space indentation and Chinese comments. Use `snake_case` for C functions and variables, `UPPER_SNAKE_CASE` for macros, and the `s_` prefix for file-private state. Keep implementation details `static` in `.c` files; expose only public types, constants, and APIs in headers. Pass module inputs explicitly (prefer `const` pointers) and return outputs through pointers or focused result structures. Do not allocate dynamically. ISR code must remain short; shared ISR/main-loop state is `volatile`.

C# uses four spaces, file-scoped namespaces, PascalCase public members/types, and `_camelCase` private fields. Follow the existing MVVM organization and nullable-reference settings.

## Testing Guidelines

No automated test framework or coverage threshold is configured. Treat a clean IAR build without warnings as the minimum firmware check, then verify affected behavior on STM32 hardware through OLED, keys, USART, Modbus, and DAC paths. For host changes, run `dotnet build` and manually exercise connection, read/write, and error states. Record hardware setup and observed results in the pull request.

## Commit & Pull Request Guidelines

History follows Conventional Commit prefixes such as `feat:`, `fix:`, `style:`, and `chore:`; use an imperative, scoped summary. Keep firmware, host, and documentation changes logically grouped. Pull requests should explain the behavior change, affected hardware/protocol registers, verification steps, and linked issue. Include screenshots for WPF or OLED UI changes. Update `README.md` version notes and related protocol/design documents before pushing behavior changes.
