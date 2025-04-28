<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
</head>
<body>
  <h1>ESP32 Bingo Game</h1>
  <p>A web-based Bingo game hosted on an ESP32 microcontroller. Players (humans and bots) connect via Wi-Fi, join a lobby, receive randomized bingo tickets, and watch the game progress in real time through Server-Sent Events (SSE).</p>

  <h2>Features</h2>
  <ul>
    <li><strong>Wi-Fi Access Point &amp; Station:</strong> ESP32 serves as both AP and station, with no power saving for reliable performance.</li>
    <li><strong>Dynamic Ticket Generation:</strong> Perfect strips of 6 tickets generated server-side using <code>std::mt19937</code> seeded by <code>esp_random()</code>.</li>
    <li><strong>Human &amp; Bot Players:</strong> Human players choose up to 6 tickets; bots automatically join with random ticket counts.</li>
    <li><strong>Real-Time Updates:</strong> Calls numbers, lines, and full house notifications broadcasted via SSE.</li>
    <li><strong>Web Interface:</strong> Responsive HTML/JS UI served from PROGMEM, showing ticket grids, called numbers, win popups, and game state.</li>
  </ul>

  <h2>Prerequisites</h2>
  <ul>
    <li><strong>Hardware:</strong> ESP32 development board.</li>
    <li><strong>Software:</strong>
      <ul>
        <li>Arduino IDE or PlatformIO</li>
        <li>ESP32 board support installed</li>
        <li>Libraries:
          <ul>
            <li><code>WiFi.h</code></li>
            <li><code>AsyncTCP.h</code></li>
            <li><code>ESPAsyncWebServer.h</code></li>
          </ul>
        </li>
      </ul>
    </li>
  </ul>

  <h2>File Overview</h2>
  <ul>
    <li><code>.ino</code> / <code>.cpp</code>: Main application contains:
      <ul>
        <li>Ticket structure and <code>TicketGenerator</code> class</li>
        <li>Wi-Fi setup and AP configuration</li>
        <li>Async web server and SSE event source</li>
        <li>Game logic: lobby, countdown, draw loop, win detection</li>
        <li>HTML/CSS/JS pages in PROGMEM for game UI and connections page</li>
      </ul>
    </li>
    <li><strong>TicketGenerator</strong> (<code>TicketGenerator</code> class)
      <ul>
        <li>Pools numbers into 9 columns (1–9, 10–19, … 80–90)</li>
        <li>Builds strips of 6 tickets with exactly 15 numbers each (5 per row)</li>
        <li>Ensures balanced distribution</li>
      </ul>
    </li>
    <li><strong>Web Interface</strong> (embedded HTML)
      <ul>
        <li><code>mainPageHtml</code>: Bingo game interface</li>
        <li>Client-side JS handles:
          <ul>
            <li>Joining lobby and requesting tickets</li>
            <li>Rendering ticket cards and marking called numbers</li>
            <li>Displaying countdown and game progress</li>
            <li>Pop-up notifications for 1-line, 2-lines, and full house wins</li>
          </ul>
        </li>
      </ul>
    </li>
  </ul>

  <h2>Installation &amp; Usage</h2>
  <ol>
    <li><strong>Clone the repository:</strong>
      <pre><code>git clone https://github.com/your-username/esp32-bingo.git
cd esp32-bingo</code></pre>
    </li>
    <li>Open project in Arduino IDE or PlatformIO.</li>
    <li>Configure Wi-Fi (optional): modify <code>ssid</code> and <code>password</code> variables (defaults to open AP).</li>
    <li>Install dependencies: use Library Manager to install <code>AsyncTCP</code> and <code>ESPAsyncWebServer</code>.</li>
    <li>Upload to ESP32.</li>
    <li>Connect:
      <ul>
        <li>Join the Wi-Fi network <code>Wifi Bingo</code>.</li>
        <li>Navigate to <code>http://192.168.4.1/</code> for the bingo game.</li>
      </ul>
    </li>
  </ol>



</body>
</html>
