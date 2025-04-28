#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#include <map>
#include <algorithm>
#include <random>
#include <set>
#include <array>          //  <-- add
#include <deque>          //  <-- add
#include "esp_system.h"   //  <-- add (for esp_random)
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <pgmspace.h>     //  <-- add for PROGMEM

//--------------------------------------------------------------------------
// Ticket structure
//--------------------------------------------------------------------------

struct Ticket {
  String serial;
  int    grid[3][9];
  bool   marked[3][9];
};

//--------------------------------------------------------------------------
// TicketGenerator – builds perfect strips of 6 tickets
//--------------------------------------------------------------------------

class TicketGenerator {
public:
  static Ticket nextTicket() {
    if (stripIndex == 6) { currentStrip = buildStrip(); stripIndex = 0; }
    return currentStrip[stripIndex++];
  }
  static void resetStrip() { currentStrip = buildStrip(); stripIndex = 0; }
private:
  static std::mt19937 &rng() { static std::mt19937 r(esp_random()); return r; }
  static std::array<std::deque<int>,9> makePools() {
    std::array<std::deque<int>,9> p;
    auto f=[&](int c,int a,int b){ for(int n=a;n<=b;++n)p[c].push_back(n);
        std::shuffle(p[c].begin(),p[c].end(),rng()); };
    f(0,1,9);
    for(int c=1;c<8;++c) f(c,c*10,c*10+9);
    f(8,80,90);
    return p;
  }
  static std::vector<Ticket> buildStrip() {
    constexpr int R=3,C=9;
    auto pools=makePools();
    std::array<std::array<std::vector<int>,C>,6> buckets{};
    int cnt[6]{};

    for(int c=0;c<C;++c)
      for(int t=0;t<6;++t){
        buckets[t][c].push_back(pools[c].front());
        pools[c].pop_front();
        ++cnt[t];
      }

    std::uniform_int_distribution<int>d6(0,5);
    while(true){
      bool full=true;
      for(int i=0;i<6;++i) if(cnt[i]<15){full=false;break;}
      if(full) break;
      int t=d6(rng());
      if(cnt[t]==15) continue;
      std::vector<int> ok;
      for(int c=0;c<C;++c)
        if(!pools[c].empty() && buckets[t][c].size()<3) ok.push_back(c);
      if(ok.empty()) return buildStrip();
      int c=ok[d6(rng())%ok.size()];
      buckets[t][c].push_back(pools[c].front());
      pools[c].pop_front();
      ++cnt[t];
    }

    static int serial=100000;
    std::vector<Ticket> strip; strip.reserve(6);

    for(int t=0;t<6;++t){
      Ticket tk; tk.serial=String(serial++);
      int rc[3]{};
      for(int r=0;r<R;++r)
        for(int c=0;c<C;++c)
          tk.grid[r][c]=tk.marked[r][c]=0;

      for(int c=0;c<C;++c){
        auto &v=buckets[t][c];
        std::sort(v.begin(),v.end());
        switch(v.size()){
          case 3:
            for(int r=0;r<R;++r){
              tk.grid[r][c]=v[r];
              ++rc[r];
            }
            break;
          case 2:{
            int r1=minRow(rc);
            int r2=(r1==0? (rc[1]<=rc[2]?1:2):(r1==1? (rc[0]<=rc[2]?0:2):(rc[0]<=rc[1]?0:1)));
            tk.grid[r1][c]=v[0];
            tk.grid[r2][c]=v[1];
            ++rc[r1]; ++rc[r2];
          } break;
          case 1:{
            int r=minRow(rc);
            tk.grid[r][c]=v[0];
            ++rc[r];
          } break;
        }
      }
      if(!(rc[0]==5&&rc[1]==5&&rc[2]==5)) return buildStrip();
      strip.push_back(tk);
    }
    return strip;
  }
  static int minRow(const int rc[3]){ return (rc[0]<=rc[1]&&rc[0]<=rc[2])?0:(rc[1]<=rc[2]?1:2);}  
  static std::vector<Ticket> currentStrip;
  static int stripIndex;
};
std::vector<Ticket> TicketGenerator::currentStrip=TicketGenerator::buildStrip();
int TicketGenerator::stripIndex=0;

//--------------------------------------------------------------------------
// Wi-Fi / Web-server game code – original (only the ticket generation
// section was changed; all network / UI code is intact.)
//--------------------------------------------------------------------------

const char *ssid = "Bingo_Game";
const char *password = "";

AsyncWebServer    server(80);
AsyncEventSource  events("/events");

bool gameStarted = false;
bool countdownStarted = false;
unsigned long countdownStartTime = 0;
const unsigned long countdownDuration = 60000; // 60s

std::vector<int>      calledNumbers;
std::vector<String>   eventHistory;

bool line1Happened = false;
bool line2Happened = false;
bool fullHouseHappened = false;

auto fullHouseRevealStart = 0UL;
const unsigned long fullHouseRevealDuration = 10000; // 10s

// still used by reset logic – harmless even if we no longer fill it
std::set<String> ticketSignatures;

struct PlayerInfo { String name; std::vector<Ticket> tickets; };
struct BotInfo    { String name; int ticketCount; std::vector<Ticket> tickets; };

std::map<String, PlayerInfo> players;
std::vector<BotInfo>         bots;

int ticketSerialCounter = 100000; // kept for consistency (TicketGenerator has its own)

// Bot config --------------------------------------------------------------
const std::vector<String> botNames = {
  "AlexBot","BenBot","CarterBot","DominicBot","ElijahBot",
  "GraceBot","HannahBot","LilyBot","VictoriaBot","ZoeBot"};
const int minBots = 2, maxBots = 10, maxTicketsPerBot = 6;
int botCount = 0;

//--------------------------------------------------------------------------
// Replaced functions – now just forward to TicketGenerator
//--------------------------------------------------------------------------
Ticket generateUniqueTicket() { return TicketGenerator::nextTicket(); }

//--------------------------------------------------------------------------
// JSON serialization
//--------------------------------------------------------------------------
String ticketToJSON(const Ticket &t) {
  String j = "{\"serial\":\"" + t.serial + "\",\"grid\":[";
  for (int r = 0; r < 3; ++r) {
    j += "[";
    for (int c = 0; c < 9; ++c) {
      j += String(t.grid[r][c]);
      if (c < 8) j += ",";
    }
    j += "]";
    if (r < 2) j += ",";
  }
  j += "]}";
  return j;
}

//--------------------------------------------------------------------------
// Event Broadcasting
//--------------------------------------------------------------------------
void notifyAllClients(const String& msg)
{
  events.send(msg.c_str(), nullptr, millis());

  /* keep only the game-relevant messages for later re-play */
  if (   msg.startsWith("CALL:")
      || msg.startsWith("WIN1:")
      || msg.startsWith("WIN2:")
      || msg.startsWith("WIN3:")
      || msg == "GAMERESET"
      || msg.startsWith("PLAYERCOUNT:")
      || msg.startsWith("BOTCOUNT:") )
  {
    const size_t MAX_HISTORY = 256;              // hard cap ≈ few KB RAM
    eventHistory.push_back(msg);
    if (eventHistory.size() > MAX_HISTORY)
      eventHistory.erase(eventHistory.begin(),
                         eventHistory.begin() + (eventHistory.size() - MAX_HISTORY));
  }
}

//--------------------------------------------------------------------------
// Reset game
//--------------------------------------------------------------------------
void endGame() {
  gameStarted = false;
  countdownStarted = false;

  calledNumbers.clear();
  players.clear();
  ticketSignatures.clear();
  eventHistory.clear();

  notifyAllClients("GAMERESET");
  notifyAllClients("PLAYERCOUNT:0");

  initializeBots();
  notifyAllClients("BOTCOUNT:" + String(botCount));

  line1Happened = line2Happened = fullHouseHappened = false;
}

//--------------------------------------------------------------------------
// Bingo Logic
//--------------------------------------------------------------------------
void drawNextNumber() {
  if (calledNumbers.size() >= 90) return;

  int n;
  do {
    n = random(1,91);
  } while (std::find(calledNumbers.begin(), calledNumbers.end(), n) != calledNumbers.end());
  calledNumbers.push_back(n);
  notifyAllClients("CALL:" + String(n));

  // mark & check humans
  for (auto& kv : players)
    for (auto& t : kv.second.tickets)
      for (int i=0; i<3; ++i)
        for (int j=0; j<9; ++j)
          if (t.grid[i][j] == n) t.marked[i][j] = true;

  for (auto& kv : players) {
    for (auto& t : kv.second.tickets) {
      int lines = 0;
      for (int i=0; i<3; ++i) {
        int cnt = 0;
        for (int j=0; j<9; ++j)
          if (t.grid[i][j] && t.marked[i][j]) cnt++;
        if (cnt == 5) lines++;
      }
      String suffix = " (" + kv.second.name + ")";
      if (!line1Happened && lines >= 1) {
        notifyAllClients("WIN1:" + t.serial + suffix);
        line1Happened = true;
        delay(10000);
      }
      if (!line2Happened && lines >= 2) {
        notifyAllClients("WIN2:" + t.serial + suffix);
        line2Happened = true;
        delay(10000);
      }
      if (!fullHouseHappened && lines == 3) {
        notifyAllClients("WIN3:" + t.serial + suffix);
        fullHouseHappened = true;
        fullHouseRevealStart = millis();
        return;
      }
    }
  }

  // mark & check bots
  for (auto& bot : bots)
    for (auto& t : bot.tickets)
      for (int i=0; i<3; ++i)
        for (int j=0; j<9; ++j)
          if (t.grid[i][j] == n) t.marked[i][j] = true;

  for (auto& bot : bots) {
    for (auto& t : bot.tickets) {
      int lines = 0;
      for (int i=0; i<3; ++i) {
        int cnt = 0;
        for (int j=0; j<9; ++j)
          if (t.grid[i][j] && t.marked[i][j]) cnt++;
        if (cnt == 5) lines++;
      }
      String suffix = " (" + bot.name + ")";
      if (!line1Happened && lines >= 1) {
        notifyAllClients("WIN1:" + t.serial + suffix);
        line1Happened = true;
        delay(10000);
      }
      if (!line2Happened && lines >= 2) {
        notifyAllClients("WIN2:" + t.serial + suffix);
        line2Happened = true;
        delay(10000);
      }
      if (!fullHouseHappened && lines == 3) {
        notifyAllClients("WIN3:" + t.serial + suffix);
        fullHouseHappened = true;
        fullHouseRevealStart = millis();
        return;
      }
    }
  }
}

void startCountdown() {
  countdownStarted = true;
  countdownStartTime = millis();
  notifyAllClients("COUNTDOWN:" + String(countdownDuration/1000));
}

void startGame() {
  gameStarted = true;
  notifyAllClients("GAMESTART");
}

void initializeBots() {
  botCount = random(minBots, maxBots + 1);
  std::vector<String> names = botNames;
  std::shuffle(names.begin(), names.end(), std::default_random_engine(millis()));
  bots.clear();
  for (int i = 0; i < botCount; ++i) {
    BotInfo b;
    b.name = names[i];
    b.ticketCount = random(1, maxTicketsPerBot + 1);
    for (int t = 0; t < b.ticketCount; ++t) {
      b.tickets.push_back(generateUniqueTicket());
    }
    bots.push_back(b);
  }
}

//────────────────────────────────────────────────────────────────────────────────
// Raw‐literal HTML in flash (PROGMEM)
//────────────────────────────────────────────────────────────────────────────────
const char mainPageHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1.0">
  <title>Bingo Game</title>
<style>
  /* ─── overall page layout (unchanged) ─── */
  body{font-family:Arial;text-align:center;background:linear-gradient(to bottom right,#90caf9,#1565c0);margin:0;padding:0;min-height:100vh;color:#000}
  h1{font-size:2em;margin:20px 0;color:#e3f2fd;text-shadow:2px 2px 4px rgba(0,0,0,0.5)}
  table{border-collapse:collapse;margin:0 auto;background:rgba(255,255,255,0.8)}
  td{width:40px;height:40px;text-align:center;border:1px solid #000;font-size:20px;background:#fff;color:#000}
  .grey{background:#ccc}.marked{background:lightgreen}
  #statusMessage{font-size:20px;margin:10px}
  #winPopup{display:none;position:fixed;top:20px;left:50%;transform:translateX(-50%);background:rgba(255,255,255,0.9);border:2px solid #333;padding:10px 20px;font-size:18px;border-radius:4px;box-shadow:0 2px 8px rgba(0,0,0,0.3);z-index:1000;color:#000}
  #lobby,#gameView{padding:10px}
  #controls input,#controls select,#controls button{font-size:1em;padding:5px;margin:5px}
  #controls button{cursor:pointer}
  #winList,#calledNumbers{color:#000}

  /* ─── ticket card look ─── */
  .ticketCard{
    position:relative;
    display:inline-block;
    margin:10px 6px;
    padding:24px 6px 4px 6px;       /* 24 px top so the serial fits inside */
    border:2px solid #333;
    border-radius:6px;
    background:rgba(255,255,255,0.9);
  }
  .ticketSerial{
    position:absolute;
    top:4px; right:8px;
    font-size:0.8em; font-weight:bold; color:#000;
  }
  .ticketRemaining {
  position: absolute;
  top: 4px;
  left: 8px;
  font-size: 0.8em;
  font-weight: bold;
  color: #000;
}
</style>

</head>
<body>
  <h1>🎱 Bingo Game</h1>

  <div id="winPopup"></div>

  <div>Real Players: <span id="playerCount">0</span></div>
  <div>Bot Players:  <span id="botCount">0</span></div>
  <div id="statusMessage">✅ Tickets available.</div>
<div id="gameProgress" style="font-size: 18px; margin-top: 5px; display: none;">
  Playing: <span id="currentStage">1 Line</span> | Drawn: <span id="numbersDrawn">0</span>/90
</div>

  <div id="lobby">
    <div id="controls">
      <label>Enter your name:</label>
      <input type="text" id="nameInput" maxlength="20"><br><br>
      <label>Choose tickets:</label>
      <select id="ticketSelect">
        <option>1</option><option>2</option><option>3</option>
        <option>4</option><option>5</option><option>6</option>
      </select>
<button id="submitBtn" type="button">Submit Tickets</button>
      <div>Tickets: <span id="ticketCount">0</span></div>
    </div>
  </div>

  <div id="gameView" style="display:none;">
    <div id="tickets"></div>
    <h2>Numbers Called</h2>
    <div id="calledNumbers"></div>
    <h2>Winning Tickets</h2>
    <ul id="winList"></ul>
  </div>

<script>
  /* ---------- globals ---------- */
  let tickets = [];
  let called  = [];
  let countdownInterval;
  let gameInProgress = false;
  let gameStage      = 1;        // 1-line → 2-lines → full-house
  const seenWins     = new Set();/* prevents duplicate pop-ups & list items */

  function updateStatus(d){
  const controls = document.getElementById('controls');
  if (d.gameStarted){
    gameInProgress = true;
    controls.style.display = 'none';
    document.getElementById('statusMessage').innerText = '❌ Game in progress.';
    document.getElementById('gameProgress').style.display = 'block';
  } else {
    gameInProgress = false;
    clearInterval(countdownInterval);
    controls.style.display = 'block';
    document.getElementById('gameProgress').style.display = 'none';
    if (d.countdownStarted) startClientCountdown(d.secondsRemaining);
    else document.getElementById('statusMessage').innerText = '✅ Tickets available.';
  }
  document.getElementById('playerCount').innerText = d.playerCount;
  document.getElementById('botCount').innerText = d.botCount;
}

function startClientCountdown(sec){
  clearInterval(countdownInterval);
  let t = sec;
  const s = document.getElementById('statusMessage');
  s.innerText = `⏳ Game starts in: ${t}s`;
  countdownInterval = setInterval(()=>{
    t--;
    if (t<=0){
      clearInterval(countdownInterval);
      gameInProgress = true;
      s.innerText = 'Game in progress...';
      document.getElementById('gameProgress').style.display = 'block';
    }else s.innerText = `⏳ Game starts in: ${t}s`;
  },1000);
}

  /* ---------- helper: adaptive score (nearest-to-*current* prize) ---------- */
  function ticketScore(t){
    /* marks in each row */
    const marks = t.grid.map(row =>
      row.reduce((s,n)=>s + (n && called.includes(n) ? 1 : 0), 0)
    );

    const linesDone   = marks.filter(m=>m === 5).length;      // 0-3
    const missingRows = marks.map(m => 5 - m);                // 0-5 each
    const totalLeft   = missingRows.reduce((a,b)=>a+b,0);     // 0-15

    const kSmallest = (arr,k)=>
          arr.slice().sort((a,b)=>a-b).slice(0,k)
              .reduce((a,b)=>a+b,0);

    /* stage 1 — first completed line */
    if (gameStage === 1){
      if (linesDone >= 1) return -100 + totalLeft;            // already a 1-liner
      return Math.min(...missingRows);                        // fewest to finish one row
    }

    /* stage 2 — two completed lines */
    if (gameStage === 2){
      if (linesDone >= 2) return -100 + totalLeft;            // already a 2-liner
      if (linesDone === 1) return Math.min(...missingRows);   // need one more line
      return kSmallest(missingRows,2);                        // need two rows
    }

    /* stage 3 — full house */
    return totalLeft;                                         // fewer blanks first
  }

  /* ---------- draw tickets with border & serial ---------- */
function showTickets(){
  tickets.sort((a,b) => ticketScore(a) - ticketScore(b));
  const div = document.getElementById('tickets');
  div.innerHTML = '';

  tickets.forEach(t => {
    // 1) count marks per row
    const marks = t.grid.map(row =>
      row.reduce((sum, n) => sum + (n && called.includes(n) ? 1 : 0), 0)
    );
    const missingRows = marks.map(m => 5 - m);

    // 2) compute total missing for current stage
    let missingCount;
    if (gameStage === 1) {
      missingCount = marks.some(m => m === 5)
        ? 0
        : Math.min(...missingRows);
    }
    else if (gameStage === 2) {
      const linesDone = marks.filter(m => m === 5).length;
      if (linesDone >= 2) {
        missingCount = 0;
      } else if (linesDone === 1) {
        missingCount = Math.min(
          ...marks.filter(m => m < 5).map(m => 5 - m)
        );
      } else {
        const sorted = missingRows.slice().sort((a,b)=>a-b);
        missingCount = sorted[0] + sorted[1];
      }
    }
    else {
      missingCount = missingRows.reduce((a,b)=>a+b, 0);
    }

    // 3) only show badge when 1, 2 or 3 to go
    let remainingHTML = '';
    if (missingCount >= 1 && missingCount <= 3) {
      remainingHTML = `<span class="ticketRemaining">${missingCount} to go</span>`;
    }

    // 4) build the card
    let html = `<div class="ticketCard">`
             + remainingHTML
             + `<span class="ticketSerial">#${t.serial}</span>`
             + `<table>`;
    t.grid.forEach(row => {
      html += '<tr>' + row.map(n =>
        `<td class="${n===0?'grey':''}">${n||''}</td>`
      ).join('') + '</tr>';
    });
    html += `</table></div>`;

    div.insertAdjacentHTML('beforeend', html);
  });

  // 5) re-highlight any called numbers
  document.querySelectorAll('#tickets td').forEach(td => {
    const n = +td.textContent;
    if (n && called.includes(n)) td.classList.add('marked');
  });
}


  function updateCalled(arr){
    called = arr;
    document.getElementById('calledNumbers').innerText = arr.join(', ');
    showTickets();
  }

  /* ---------- winner helpers ---------- */
function addToWinList(type, txt) {
  // build a human-readable label
  const label = type === 'WIN1' ? '1 Line'
              : type === 'WIN2' ? '2 Lines'
              : 'Full House';

  // append a new <li> to the winList
  const li = document.createElement('li');
  li.innerText = `${label} Win – Ticket ${txt}`;
  document.getElementById('winList').appendChild(li);
}

  function showWinPopup(type, txt){
    const label = type==='WIN1' ? '1 Line'
                : type==='WIN2' ? '2 Lines' : 'Full House';
    const pop = document.getElementById('winPopup');
    pop.textContent = (type==='WIN3' ? '🏆 ' : '🎉 ') + label + ' Win! ' + txt;
    pop.style.display = 'block';
    clearTimeout(showWinPopup._timer);
    showWinPopup._timer = setTimeout(()=>pop.style.display='none', 5000);
  }
  function hideWinPopup(){ document.getElementById('winPopup').style.display='none'; }



  /* ---------- initial load ---------- */
window.addEventListener('load', () => {
  // restore saved name
  const saved = localStorage.getItem('bingoPlayerName');
  if (saved) document.getElementById('nameInput').value = saved;

  // fetch both game status and player state in parallel
  Promise.all([
    fetch('/game_status').then(r => r.json()),
    fetch('/player_state').then(r => r.json())
  ])
  .then(([status, state]) => {
    // 1) Show/hide lobby or countdown based on whether the game is running
    updateStatus(status);

    // ─── replay the current draw for everyone ───
    if (state.called && state.called.length) {
      updateCalled(state.called);
      document.getElementById('numbersDrawn').innerText = state.called.length;
    }

    // ─── replay any wins for everyone ───
    if (state.wins && state.wins.length) {
      state.wins.forEach(w => {
        const [type, rest] = w.split(/:(.+)/);
        addToWinList(type, rest);
        seenWins.add(w);
        if (type === 'WIN1') gameStage = 2;
        else if (type === 'WIN2') gameStage = 3;
      });
    }

    // 2) If the game is in progress but the user has NO tickets → show progress bar
    if (status.gameStarted && (!state.tickets || state.tickets.length === 0)) {
      // hide the “choose tickets” lobby, display the game view
      document.getElementById('lobby').style.display     = 'none';
      document.getElementById('controls').style.display  = 'none';
      document.getElementById('gameView').style.display  = 'block';
      // make sure the progress bar is visible
      document.getElementById('gameProgress').style.display = 'block';
      // set the numbers drawn count
      document.getElementById('numbersDrawn').innerText = status.numbersDrawn || 0;
    }

    // 3) If the player _does_ have tickets (joined before), restore them:
    if (state.tickets && state.tickets.length) {
      tickets = state.tickets;
      called  = state.called || [];
      document.getElementById('ticketCount').innerText = tickets.length;
      document.getElementById('lobby').style.display    = 'none';
      document.getElementById('gameView').style.display = 'block';

      // show the progress bar once we have the numbers
      document.getElementById('gameProgress').style.display = status.gameStarted ? 'block' : 'none';
      document.getElementById('numbersDrawn').innerText     = called.length;

      // redraw the tickets (this also marks numbers & sorts them)
      updateCalled(called);

      // restore any wins they’d already seen
      if (state.wins && state.wins.length) {
        state.wins.forEach(w => {
          const [type, rest] = w.split(/:(.+)/);
          addToWinList(type, rest);
          if      (type === 'WIN1') gameStage = 2;
          else if (type === 'WIN2') gameStage = 3;
        });
      }
    }
  });
});

  /* ---------- ticket request ---------- */
  document.getElementById('submitBtn').addEventListener('click', () => {
    const name = document.getElementById('nameInput').value.trim();
    if (!name)          return alert('Enter name!');
    if (gameInProgress) return alert('❌ Game in progress.');
    localStorage.setItem('bingoPlayerName', name);

    const count = +document.getElementById('ticketSelect').value;
    tickets = []; seenWins.clear();
    document.getElementById('ticketCount').innerText = 0;
    document.getElementById('lobby').style.display    = 'none';
    document.getElementById('gameView').style.display = 'block';

    for (let i = 0; i < count; ++i) {
      fetch(`/get_ticket?name=${encodeURIComponent(name)}`)
        .then(r => r.json())
        .then(t => {
          tickets.push(t);
          document.getElementById('ticketCount').innerText = tickets.length;
          showTickets();
        });
    }
  });

    <!-- ─────────────────── Server-Sent Events (SSE) ─────────────────── -->
  const evt = new EventSource('/events');
  evt.onmessage = e => {
    const d = e.data;

    // — Update player/bot counts —
    if (d.startsWith('PLAYERCOUNT:')) {
      document.getElementById('playerCount').innerText = d.split(':')[1];
      return;
    }
    if (d.startsWith('BOTCOUNT:')) {
      document.getElementById('botCount').innerText = d.split(':')[1];
      return;
    }

    // — Countdown → Game start —
    if (d.startsWith('COUNTDOWN:')) {
      gameInProgress = false;
      startClientCountdown(+d.split(':')[1]);
      return;
    }
    if (d === 'GAMESTART') {
      gameStage = 1;
      document.getElementById('currentStage').innerText = '1 Line';
      gameInProgress = true;
      clearInterval(countdownInterval);
      document.getElementById('statusMessage').innerText = 'Game in progress...';
      document.getElementById('gameProgress').style.display = 'block';
      document.getElementById('lobby').style.display = 'none';
      document.getElementById('controls').style.display = 'none';
      document.getElementById('gameView').style.display = 'block';
      return;
    }

    // — Game reset —
    if (d === 'GAMERESET') {
      hideWinPopup();
      gameStage = 1;
      gameInProgress = false;
      clearInterval(countdownInterval);
      called = [];
      tickets = [];
      seenWins.clear();
      document.getElementById('lobby').style.display = 'block';
      document.getElementById('controls').style.display = 'block';
      document.getElementById('gameView').style.display = 'none';
      document.getElementById('ticketCount').innerText = 0;
      document.getElementById('tickets').innerHTML = '';
      document.getElementById('calledNumbers').innerHTML = '';
      document.getElementById('winList').innerHTML = '';
      document.getElementById('statusMessage').innerText = '✅ Tickets available.';
      document.getElementById('currentStage').innerText = '1 Line';
      document.getElementById('numbersDrawn').innerText = '0';
      document.getElementById('gameProgress').style.display = 'none';
      return;
    }

    // — Number draw —
    if (d.startsWith('CALL:')) {
      const n = +d.split(':')[1];
      if (!called.includes(n)) {
        called.push(n);
        updateCalled(called);
        document.getElementById('numbersDrawn').innerText = called.length;
      }
      return;
    }

    // — Wins —
    if (d.startsWith('WIN')) {
      if (seenWins.has(d)) return;
      const [type, rest] = d.split(/:(.+)/);

      if (type === 'WIN1') {
        gameStage = 2;
        document.getElementById('currentStage').innerText = '2 Lines';
      } else if (type === 'WIN2') {
        gameStage = 3;
        document.getElementById('currentStage').innerText = 'Full House';
      } else if (type === 'WIN3') {
        document.getElementById('currentStage').innerText = 'Full House Complete!';
      }

      addToWinList(type, rest);
      showWinPopup(type, rest);
      showTickets();
      seenWins.add(d);
      return;
    }
  };
</script>
</body>
</html>
)rawliteral";

const char connectionsPageHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Connections</title>
  <style>
    body{font-family:Arial;text-align:center;background:#f0f0f0;padding:20px;}
    h1{font-size:24px;color:#333;margin-bottom:10px;}
    table{border-collapse:collapse;margin:0 auto 20px;width:80%;}
    th,td{border:1px solid #000;padding:10px;font-size:18px;text-align:center;}
    th{background:#eee;}
  </style>
</head>
<body>
  <h1>Connected Clients</h1>
  <table id="connectionsTable">
    <tr><th>MAC Address</th><th>IP Address</th></tr>
  </table>
  <script>
    const evtConn = new EventSource('/events');
    evtConn.addEventListener('connection', e => {
      const [mac, ip] = e.data.split(',');
      const tbl = document.getElementById('connectionsTable');
      const row = tbl.insertRow(-1);
      row.insertCell(0).textContent = mac;
      row.insertCell(1).textContent = ip;
    });
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.softAP(ssid, password);
  Serial.print("Bingo AP started, IP = ");
  Serial.println(WiFi.softAPIP());

  initializeBots();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send_P(200, "text/html", mainPageHtml);
  });
  server.on("/game_status", HTTP_GET, [](AsyncWebServerRequest* req){
    unsigned long elapsed = countdownStarted ? millis() - countdownStartTime : 0;
    int rem = countdownStarted ? max(0, int((countdownDuration - elapsed)/1000)) : 0;
    String s = String("{") +
               "\"gameStarted\":"    + (gameStarted    ? "true":"false") + "," +
               "\"countdownStarted\":" + (countdownStarted?"true":"false") + "," +
               "\"secondsRemaining\":"  + rem + "," +
               "\"playerCount\":"       + players.size() + "," +
               "\"botCount\":"          + botCount +
               "}";
    req->send(200, "application/json", s);
  });
  server.on("/get_ticket", HTTP_GET, [](AsyncWebServerRequest* req){
    if (gameStarted) {
      req->send(403, "text/plain", "Game in progress.");
      return;
    }
    String name = req->hasParam("name") ? req->getParam("name")->value() : "Anonymous";
    String ip   = req->client()->remoteIP().toString();
    if (players[ip].tickets.empty()) TicketGenerator::resetStrip();
    if (players[ip].tickets.size() >= 6) {
      req->send(403, "text/plain", "❌ Limit is 6 tickets per player.");
      return;
    }
    Ticket t = generateUniqueTicket();
    players[ip].name = name;
    players[ip].tickets.push_back(t);
    req->send(200, "application/json", ticketToJSON(t));
    notifyAllClients("PLAYERCOUNT:" + String(players.size()));
    if (!countdownStarted) startCountdown();
  });
  server.on("/player_state", HTTP_GET, [](AsyncWebServerRequest* req){
    String ip = req->client()->remoteIP().toString();
    auto it = players.find(ip);
    String json = "{\"tickets\":[";
    if (it != players.end()) {
      const auto& v = it->second.tickets;
      for (size_t i = 0; i < v.size(); ++i) {
        json += ticketToJSON(v[i]);
        if (i + 1 < v.size()) json += ",";
      }
    }
    json += "],\"called\":[";
    for (size_t i = 0; i < calledNumbers.size(); ++i) {
      json += String(calledNumbers[i]);
      if (i + 1 < calledNumbers.size()) json += ",";
    }
    json += "],\"wins\":[";
    bool first = true;
    for (const String& m : eventHistory) {
      if (m.startsWith("WIN")) {
        if (!first) json += ",";
        json += "\"" + m + "\"";
        first = false;
      }
    }
    json += "]}";
    req->send(200, "application/json", json);
  });
  server.on("/connections", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send_P(200, "text/html", connectionsPageHtml);
  });

  server.addHandler(&events);
  server.begin();
}

void loop() {
  if (fullHouseHappened) {
    if (millis() - fullHouseRevealStart >= fullHouseRevealDuration) {
      endGame();
    }
    return;
  }
  if (countdownStarted && !gameStarted &&
      millis() - countdownStartTime > countdownDuration) {
    startGame();
  }
  static unsigned long lastDraw = 0;
  if (gameStarted && millis() - lastDraw > 3000) {
    drawNextNumber();
    lastDraw = millis();
  }
  if (gameStarted && calledNumbers.size() >= 90) {
    static unsigned long endT = millis();
    if (millis() - endT > 10000) {
      endGame();
    }
  }
}
