ESP32 Bingo Game

A web‑based Bingo game hosted on an ESP32 microcontroller. Players (humans and bots) connect via Wi‑Fi, join a lobby, receive randomized bingo tickets, and watch the game progress in real time through Server‑Sent Events (SSE).

Features

Wi‑Fi Access Point & Station: ESP32 serves as both AP and station, with no power saving for reliable performance.

Dynamic Ticket Generation: Perfect strips of 6 tickets generated server‑side using std::mt19937 seeded by esp_random().

Human & Bot Players: Human players choose up to 6 tickets; bots automatically join with random ticket counts.

Real‑Time Updates: Calls numbers, lines, and full house notifications broadcasted via SSE.

Web Interface: Responsive HTML/JS UI served from PROGMEM, showing ticket grids, called numbers, win popups, and game state.

Connection Monitor: Separate page to view connected clients (MAC & IP).

Prerequisites

Hardware: ESP32 development board.

Software:

Arduino IDE or PlatformIO

ESP32 board support installed

Libraries:

WiFi.h

AsyncTCP.h

ESPAsyncWebServer.h

File Overview

.ino / .cpp: Main application contains:

Ticket structure and TicketGenerator class

Wi‑Fi setup and AP configuration

Async web server and SSE event source

Game logic: lobby, countdown, draw loop, win detection

HTML/CSS/JS pages in PROGMEM for game UI and connections page

TicketGenerator (TicketGenerator class)

Pools numbers into 9 columns (1–9, 10–19, … 80–90)

Builds strips of 6 tickets with exactly 15 numbers each (5 per row)

Ensures balanced distribution

Web Interface (embedded HTML)

mainPageHtml: Bingo game interface

connectionsPageHtml: Table of connected clients

Client‑side JS handles:

Joining lobby and requesting tickets

Rendering ticket cards and marking called numbers

Displaying countdown and game progress

Pop‑up notifications for 1‑line, 2‑lines, and full house wins

Installation & Usage

Clone the repository:

git clone https://github.com/your‑username/esp32‑bingo.git
cd esp32‑bingo

Open project in Arduino IDE or PlatformIO.

Configure Wi‑Fi (optional):

Modify ssid and password variables (defaults to open AP).

Install dependencies:

In Arduino IDE: use Library Manager to install AsyncTCP and ESPAsyncWebServer.

Upload to ESP32.

Connect:

Join the Wi‑Fi network Wifi Bingo.

Navigate to http://192.168.4.1/ for the bingo game.
