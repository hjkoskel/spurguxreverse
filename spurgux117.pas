program SpurguX;
{ ===========================================================================
  SpurguX v1.17 -- clean-room re-implementation in Free Pascal.

  Reconstructed from the original DOS binaries (spurg117/SPU.COM v1.17 and
  spurg224/SPU.EXE v2.24), the shipped data files and spurgudesign.md.
  No original source code was used.

  Game rules follow the v1.17 disassembly:
    * level generator   (file 0x4216..0x4D20): building flags, spawn rolls
                         and HP formulas for every creature and item
    * turn processor    (file 0x5473..0x7E50): pick-ups, creature fights,
                         ALCO shop, DIVARI pawn shop, building doors
    * NPC update chain  (file 0x9EC4..0xE0E0): what each creature does to
                         the player when standing next to it
    * per-turn handler  (file 0xE0E8..0xE36A): drunk state, Promille decay,
                         Kankkunen, Voima regeneration, AIDS drain
  Additions merged from v2.24 (marked "v2.24"):
    S = save game and quit, p = repeat last message, c = clock,
    Ctrl-L = redraw screen.

  Build:  fpc spurgux117.pas
  Run:    ./spurgux117          (data files SPU.HLP/.SPE/.TIP/.TOP are
                                 looked up in ., the program dir and
                                 ./spurg117)
  Options: --mono (no colours), --seed N (repeatable game),
           --level N / --mk N (debug: start deeper / with money),
           --dumpmap FILE (debug: dump level 1 tile bytes and exit)

  Original: (C) 1987 Petri Niska. This is an independent clean-room port.
  =========================================================================== }
{$mode objfpc}
{$H+}
uses SysUtils, Classes, BaseUnix;

const
  SCRW = 80; SCRH = 25;

  MSG_ROW   = 1;
  FRAME_TOP = 2;  FRAME_BOT = 23;
  FRAME_L   = 13; FRAME_R   = 80;
  STAT_ROW  = 25;
  MAP_X0 = 14; MAP_X1 = 79;
  MAP_Y0 = 3;  MAP_Y1 = 22;

  { random-placement rectangle of the original placement helper (0x2DF3):
    X = 23+Random(56), Y = 7+Random(12) }
  PLACE_X0 = 23; PLACE_Y0 = 7;

  { --- tile codes (exactly the original's word values) --- }
  T_FLOOR    = $00;
  T_KALJA    = $01;  { %  beer }
  T_LONKKA   = $02;  { &  long drink }
  T_HAALARI  = $03;  { Σ  overalls }
  T_PAMPPU   = $04;  { !  baton }
  T_STILETTI = $05;  { \  stiletto }
  T_KETJUT   = $06;  { ß  chains }
  T_CAMEL    = $07;  { Θ  Camel boots }
  T_KONJAKKI = $0B;  { Φ  cognac (level 50) }
  T_MONEY    = $0C;  { $  money }
  T_HINT     = $0D;  { ¿  hint }
  T_MILK     = $0E;  { ⌂  milk }
  T_CHOCO    = $0F;  { ∞  chocolate }
  T_APPLE    = $10;  { o  apple }
  T_SILAKKA  = $11;  { ¥  herring }
  T_MAKKARA  = $12;  { (  sausage ring }
  T_DOT      = $14;  { .  dot }
  T_EXIT     = $15;  { >  gate }
  T_WATER    = $16;  { ≈  water }
  T_POOP     = $17;  { æ  "kökköä" }
  T_POLICE1  = $1E;  { C  five police slots $1E..$22 }
  T_POLICE5  = $22;
  T_MUMMO    = $23;  { M }
  T_KAKE     = $24;  { K }
  T_VARAS    = $25;  { V }
  T_TEEKKARI = $26;  { T }
  T_PORTSARI = $27;  { Ω }
  T_NEEKERI  = $28;  { N }
  T_HINTTI   = $29;  { H }
  T_TROKARI  = $2A;  { Q }
  T_PAPPI    = $2B;  { R }
  T_HIPPI    = $2C;  { I }
  T_PIRKKO   = $2D;  { B  dot-eater }
  T_DOG      = $2E;  { D }
  T_GORILLA  = $2F;  { G }
  T_SKIN     = $30;  { O }
  T_SNAKE    = $31;  { S }
  T_AIDS     = $32;  { A }
  T_PUNK     = $33;  { P }
  T_KINGI    = $34;  { Ñ }
  T_JUPPI    = $35;  { J }
  T_KARKURI  = $36;  { E }
  T_GHOST    = $4B;  { invisible }
  T_GRAVE    = $5B;  { ±  grave (graveyard levels) }
  T_FENCE    = $5C;  { #  electric fence (zoo levels) }
  T_WALL     = $63;
  T_MAXCODE  = $63;

  START_X = 15; START_Y = 12;
  EXIT_X  = 78; EXIT_Y  = 21;
  PORTSARI_X = 79; PORTSARI_Y = 21;
  KONJAKKI_X = 78; KONJAKKI_Y = 5;
  MAX_LEVEL = 50;

  { equipment entries (name table 0x2FE + i*10); the inventory shows
    entries 3..11 as items 1..9, so a digit d always means entry d+2 }
  E_HAALARI = 3; E_PAMPPU = 4; E_STILETTI = 5; E_KETJUT = 6; E_CAMEL = 7;
  E_APY = 10; E_HENNESSY = 11;

  ST_AIDS    = 'AIDS painaa!';
  ST_BLANK   = '            ';
  ST_SICK    = 'Voit pahoin!';
  ST_VOMIT   = 'Oksennat!   ';
  ST_STAGGER = 'Horjahtelet ';
  KANKKUNEN  = 'Kankkunen saapuu ja katkaisee taipaleesi ...';

  { colour attributes }
  A_NORM = 0; A_BOLD = 1; A_RED = 2; A_GREEN = 3; A_YELLOW = 4; A_BLUE = 5;
  A_MAGENTA = 6; A_CYAN = 7; A_WHITE = 8; A_DIM = 9;

type
  TTile = Word;
  TMap = array[0..SCRW+1, 0..SCRH] of TTile;

var
  Map: TMap;
  { --- player --- }
  PX, PY: Integer;
  Level, Mk: Integer;
  Voima, Prom, Turn: Double;
  Score: Double;            { [0x1132] hidden score real (shown only in SPU.TOP) }
  { --- level bookkeeping --- }
  DotsNeeded, DotsGot: Integer;
  { --- inventory --- }
  InvKalja, InvLonkka, InvKiekura: Integer;
  Equip: array[0..11] of string;
  Weapon: Integer;          { [0x2F6] }
  Kokemus: Integer;         { [0x2EC] experience, shown on the status line;
                              grows (with luck) when a fight is won }
  { --- per-tile creature table (original 0xD18 + tile*6) --- }
  HP: array[0..T_MAXCODE] of Integer;
  { --- flags --- }
  PoliceActive: Boolean;    { [0x2D0] }
  Friendly: Boolean;        { [0x2CE] hippie trip / hallucination }
  FriendlyTurns: Integer;
  Aids: Boolean;            { [0x2CF] }
  Blind: Boolean;           { [0x2CD] = 0 }
  PortsariOn: Boolean;      { [0x2D8] }
  PunksOn: Boolean;         { [0x2E3] }
  KingX, KingY: Integer;    { [0xE52], [0xE54] punk king position }
  PunkUnder: array[0..4] of TTile;  { tiles hidden under the gang }
  FlagAlco, FlagDivari, FlagPoliisi, FlagGrave, FlagZoo, FlagAlcove: Boolean;
  HotelName: Boolean;       { SAIRAALA / HOTELLI }
  { --- ui state --- }
  MsgLine, LastMsg: string;
  StateText: string;
  Dead, Won, Saved: Boolean;
  DeathCause: string;
  DeathPre: string;
  Killer: string;           { v2.24 style "kuolinsyy": who hurt you last }         { message shown before the death cause }
  PlayerName: string;
  UseColor: Boolean = True;
  DataDirs: TStringList;

{ ===========================================================================
  Terminal I/O (raw termios + an 80x25 screen buffer with diff output)
  =========================================================================== }
const
  ICANON = $2; ECHO = $8; ISIG = $1; IEXTEN = $8000; IXON = $400;
  VMIN = 6; VTIME = 5; TCSANOW = 0;

type
  TTermios = record
    c_iflag, c_oflag, c_cflag, c_lflag: LongWord;
    c_line: Byte;
    c_cc: array[0..31] of Byte;
    pad: array[0..2] of Byte;
    c_ispeed, c_ospeed: LongWord;
  end;
  PTermios = ^TTermios;

function tcgetattr_(fd: Integer; t: PTermios): Integer; cdecl;
  external 'c' name 'tcgetattr';
{ pass pointers: a `const` record parameter is miscompiled by FPC 3.2.2 }
function tcsetattr_(fd, how: Integer; t: PTermios): Integer; cdecl;
  external 'c' name 'tcsetattr';

var
  SavedTerm: TTermios;
  TermInited: Boolean = False;
  ScreenInited: Boolean = False;

procedure OutStr(const s: string);
var p, n, w: Integer;
begin
  p := 1; n := Length(s);
  while p <= n do
  begin
    w := fpWrite(1, s[p], n - p + 1);
    if w <= 0 then Exit;
    Inc(p, w);
  end;
end;

procedure SetRaw;
var t: TTermios;
begin
  if TermInited then Exit;
  if tcgetattr_(0, @t) <> 0 then Exit;
  SavedTerm := t;
  t.c_lflag := t.c_lflag and not (ICANON or ECHO or ISIG or IEXTEN);
  t.c_iflag := t.c_iflag and not IXON;
  t.c_cc[VMIN]  := 1;
  t.c_cc[VTIME] := 0;
  tcsetattr_(0, TCSANOW, @t);
  TermInited := True;
end;

procedure RestoreTerm;
begin
  if ScreenInited then
  begin
    OutStr(#27'[0m'#27'[?25h'#27'[?1049l');
    ScreenInited := False;
  end;
  if TermInited then
  begin
    tcsetattr_(0, TCSANOW, @SavedTerm);
    TermInited := False;
  end;
end;

procedure SignalHandler(sig: cint); cdecl;
begin
  RestoreTerm;
  Halt(1);
end;

type
  TCell = record
    ch: string[4];
    at: Byte;
  end;

var
  Scr, Shown: array[1..SCRH, 1..SCRW] of TCell;
  ForceRedraw: Boolean = True;
  CurCol: Integer = 1;
  CurRow: Integer = 1;

function U8Len(c: Char): Integer;
begin
  case Ord(c) of
    $00..$7F: Result := 1;
    $C0..$DF: Result := 2;
    $E0..$EF: Result := 3;
    $F0..$F7: Result := 4;
  else Result := 1;
  end;
end;

function ULen(const s: string): Integer;
{ number of displayed characters (UTF-8 code points) }
var i: Integer;
begin
  Result := 0; i := 1;
  while i <= Length(s) do
  begin
    Inc(i, U8Len(s[i]));
    Inc(Result);
  end;
end;

procedure SPut(col, row: Integer; const s: string; at: Byte = A_NORM);
var i, n: Integer;
begin
  i := 1;
  while (i <= Length(s)) do
  begin
    n := U8Len(s[i]);
    if (row >= 1) and (row <= SCRH) and (col >= 1) and (col <= SCRW) then
    begin
      Scr[row, col].ch := Copy(s, i, n);
      Scr[row, col].at := at;
    end;
    Inc(i, n);
    Inc(col);
  end;
end;

procedure SFill(c0, r0, c1, r1: Integer);
var r, c: Integer;
begin
  for r := r0 to r1 do
    for c := c0 to c1 do
      if (r >= 1) and (r <= SCRH) and (c >= 1) and (c <= SCRW) then
      begin
        Scr[r, c].ch := ' ';
        Scr[r, c].at := A_NORM;
      end;
end;

procedure SClear;
begin
  SFill(1, 1, SCRW, SCRH);
end;

function AttrSeq(at: Byte): string;
begin
  if not UseColor then
  begin
    if at in [A_BOLD, A_WHITE] then Result := #27'[0;1m' else Result := #27'[0m';
    Exit;
  end;
  case at of
    A_BOLD:    Result := #27'[0;1m';
    A_RED:     Result := #27'[0;1;31m';
    A_GREEN:   Result := #27'[0;1;32m';
    A_YELLOW:  Result := #27'[0;1;33m';
    A_BLUE:    Result := #27'[0;1;34m';
    A_MAGENTA: Result := #27'[0;1;35m';
    A_CYAN:    Result := #27'[0;36m';
    A_WHITE:   Result := #27'[0;1;37m';
    A_DIM:     Result := #27'[0;2m';
  else Result := #27'[0m';
  end;
end;

var
  CursorOn: Boolean = False;  { terminal cursor shown only while typing an answer }

procedure SFlush;
{ emit only the cells that changed since the last flush, as one write }
var
  r, c, lastR, lastC: Integer;
  lastAt: Integer;
  o: string;
begin
  o := '';
  lastR := -1; lastC := -1; lastAt := -1;
  if ForceRedraw then
  begin
    o := #27'[0m'#27'[2J';
    lastAt := A_NORM;
  end;
  for r := 1 to SCRH do
    for c := 1 to SCRW do
      if ForceRedraw or (Scr[r, c].ch <> Shown[r, c].ch) or
         (Scr[r, c].at <> Shown[r, c].at) then
      begin
        if (r <> lastR) or (c <> lastC) then
          o := o + #27'[' + IntToStr(r) + ';' + IntToStr(c) + 'H';
        if Scr[r, c].at <> lastAt then
        begin
          o := o + AttrSeq(Scr[r, c].at);
          lastAt := Scr[r, c].at;
        end;
        if Scr[r, c].ch = '' then o := o + ' ' else o := o + Scr[r, c].ch;
        Shown[r, c] := Scr[r, c];
        lastR := r; lastC := c + 1;
      end;
  ForceRedraw := False;
  o := o + #27'[0m' + #27'[' + IntToStr(CurRow) + ';' + IntToStr(CurCol) + 'H';
  { A visible block cursor parked on the player made '@' look inverted on
    some turns and not on others, so it is hidden during play. }
  if CursorOn then o := o + #27'[?25h' else o := o + #27'[?25l';
  OutStr(o);
end;

{ --- keyboard --- }
function ReadByte(var b: Byte): Boolean;
begin
  Result := fpRead(0, b, 1) = 1;
end;

function ReadByteTimeout(tenths: Integer; var b: Byte): Boolean;
var t: TTermios;
begin
  Result := False;
  if tcgetattr_(0, @t) <> 0 then Exit;
  t.c_cc[VMIN] := 0; t.c_cc[VTIME] := Byte(tenths);
  tcsetattr_(0, TCSANOW, @t);
  Result := fpRead(0, b, 1) = 1;
  t.c_cc[VMIN] := 1; t.c_cc[VTIME] := 0;
  tcsetattr_(0, TCSANOW, @t);
end;

function RemapEscKey(c: Char): Char;
{ v1.17 ESC + key remap (dispatcher file 0x92D0..0x9393) }
begin
  case c of
    'G': Result := 'y';  'H': Result := 'k';  'I': Result := 'u';
    'K': Result := 'h';  'M': Result := 'l';  'O': Result := 'b';
    'P': Result := 'j';  'Q': Result := 'n';  'R': Result := 'i';
    'S': Result := '.';
  else Result := 'W';
  end;
end;

function ReadKey: Char;
{ flushes the screen, then reads one key. Arrow keys -> h/j/k/l,
  Home/End/PgUp/PgDn -> y/b/u/n, Insert -> i, Delete -> . }
var ch, e1, e2, e3: Byte;
begin
  SFlush;
  Result := #0;
  if not ReadByte(ch) then
  begin
    { stdin closed: nothing more can be played }
    RestoreTerm;
    Halt(0);
  end;
  if ch <> $1B then begin Result := Char(ch); Exit; end;
  if not ReadByteTimeout(1, e1) then begin Result := #27; Exit; end;
  if (e1 <> Ord('[')) and (e1 <> Ord('O')) then
  begin
    Result := RemapEscKey(Char(e1));
    Exit;
  end;
  if not ReadByteTimeout(1, e2) then
  begin
    if e1 = Ord('O') then Result := RemapEscKey('O');
    Exit;
  end;
  case Char(e2) of
    'A': Result := 'k';
    'B': Result := 'j';
    'C': Result := 'l';
    'D': Result := 'h';
    'H': Result := 'y';
    'F': Result := 'b';
    'E': Result := '.';
    '1'..'9':
      begin
        e3 := 0;
        repeat
          if not ReadByteTimeout(1, e3) then Break;
        until (e3 = Ord('~')) or (e3 >= $40);
        case Char(e2) of
          '1', '7': Result := 'y';
          '4', '8': Result := 'b';
          '5': Result := 'u';
          '6': Result := 'n';
          '2': Result := 'i';
          '3': Result := '.';
        end;
      end;
  end;
end;

{ ===========================================================================
  Data files (CP437 text)
  =========================================================================== }
function CP437ToUTF8(const s: string): string;
const
  Hi: array[$80..$FF] of string = (
    'Ç','ü','é','â','ä','à','å','ç','ê','ë','è','ï','î','ì','Ä','Å',
    'É','æ','Æ','ô','ö','ò','û','ù','ÿ','Ö','Ü','¢','£','¥','₧','ƒ',
    'á','í','ó','ú','ñ','Ñ','ª','º','¿','⌐','¬','½','¼','¡','«','»',
    '░','▒','▓','│','┤','╡','╢','╖','╕','╣','║','╗','╝','╜','╛','┐',
    '└','┴','┬','├','─','┼','╞','╟','╚','╔','╩','╦','╠','═','╬','╧',
    '╨','╤','╥','╙','╘','╒','╓','╫','╪','┘','┌','█','▄','▌','▐','▀',
    'α','ß','Γ','π','Σ','σ','µ','τ','Φ','Θ','Ω','δ','∞','φ','ε','∩',
    '≡','±','≥','≤','⌠','⌡','÷','≈','°','∙','·','√','ⁿ','²','■',' ');
var i: Integer;
begin
  Result := '';
  for i := 1 to Length(s) do
    if Ord(s[i]) >= $80 then Result := Result + Hi[Ord(s[i])]
    else if s[i] = #$7F then Result := Result + '⌂'
    else if s[i] = #9 then Result := Result + '        '
    else if s[i] >= ' ' then Result := Result + s[i];
end;

function FindData(const name: string): string;
var i: Integer;
begin
  for i := 0 to DataDirs.Count - 1 do
    if FileExists(DataDirs[i] + name) then
    begin
      Result := DataDirs[i] + name;
      Exit;
    end;
  Result := '';
end;

function LoadLines(const name: string; L: TStrings): Boolean;
{ loads a CP437 text file, splitting on CR LF, lone CR or lone LF }
var
  fn, raw, cur: string;
  fs: TFileStream;
  i: Integer;
begin
  L.Clear;
  fn := FindData(name);
  Result := fn <> '';
  if not Result then Exit;
  fs := TFileStream.Create(fn, fmOpenRead or fmShareDenyNone);
  try
    SetLength(raw, fs.Size);
    if fs.Size > 0 then fs.ReadBuffer(raw[1], fs.Size);
  finally
    fs.Free;
  end;
  cur := '';
  i := 1;
  while i <= Length(raw) do
  begin
    if raw[i] = #13 then
    begin
      L.Add(CP437ToUTF8(cur)); cur := '';
      if (i < Length(raw)) and (raw[i + 1] = #10) then Inc(i);
    end
    else if raw[i] = #10 then
    begin
      L.Add(CP437ToUTF8(cur)); cur := '';
    end
    else if raw[i] = #26 then Break
    else cur := cur + raw[i];
    Inc(i);
  end;
  if cur <> '' then L.Add(CP437ToUTF8(cur));
end;

{ ===========================================================================
  Messages and prompts
  =========================================================================== }
procedure DrawMsg;
begin
  SFill(1, MSG_ROW, SCRW, MSG_ROW);
  SPut(1, MSG_ROW, Copy(MsgLine, 1, 400), A_NORM);
  CurRow := MSG_ROW;
  CurCol := ULen(MsgLine) + 1;
  if CurCol > SCRW then CurCol := SCRW;
end;

procedure Msg(const s: string);
{ adds a message to this turn's message line (several events per turn are
  joined when they fit on the 80-column line) }
begin
  if s = '' then Exit;
  if MsgLine = '' then MsgLine := s
  else if ULen(MsgLine) + 1 + ULen(s) <= SCRW then MsgLine := MsgLine + ' ' + s
  else MsgLine := s;
  LastMsg := MsgLine;
end;

procedure SetMsg(const s: string);
begin
  MsgLine := s;
  if s <> '' then LastMsg := s;
end;

function tcflush_(fd, queue: Integer): Integer; cdecl;
  external 'c' name 'tcflush';

function ReadKeyAfterPause(ms: Integer): Char;
{ shows the screen, waits, throws away keys typed meanwhile (a held-down
  key auto-repeats), then waits for a fresh key press. Used on the end-of-
  game screens so they cannot be skipped by accident. }
begin
  SFlush;
  Sleep(ms);
  tcflush_(0, 0);              { TCIFLUSH }
  Result := ReadKey;
end;

procedure PressAny;
{ "<lisää>" prompt at row 1 col 73 (original 0x2E82) }
begin
  SFill(73, 1, SCRW, 1);
  SPut(73, 1, '<lisää>', A_BOLD);
  CurRow := 1; CurCol := 80;
  ReadKey;
  SFill(73, 1, SCRW, 1);
end;

procedure PressAnySlow;
{ PressAny for the death/win messages }
begin
  SFill(73, 1, SCRW, 1);
  SPut(73, 1, '<lisää>', A_BOLD);
  CurRow := 1; CurCol := 80;
  ReadKeyAfterPause(1000);
  SFill(73, 1, SCRW, 1);
end;

function AskKey(const q: string): Char;
begin
  MsgLine := q;
  DrawMsg;
  CursorOn := True;
  Result := ReadKey;
  CursorOn := False;
end;

function AskYesNo(const q: string): Boolean;
var a: Char;
begin
  repeat
    a := AskKey(q);
  until a in ['k', 'K', 'e', 'E', 'y', 'Y', 'n', 'N', #27];
  Result := a in ['k', 'K', 'y', 'Y'];
end;

{ ===========================================================================
  Rendering
  =========================================================================== }
function TileGlyph(t: TTile; var at: Byte): string;
begin
  at := A_NORM;
  case t of
    T_FLOOR:    Result := ' ';
    T_KALJA:    begin Result := '%'; at := A_YELLOW; end;
    T_LONKKA:   begin Result := '&'; at := A_YELLOW; end;
    T_HAALARI:  begin Result := 'Σ'; at := A_CYAN; end;
    T_PAMPPU:   begin Result := '!'; at := A_CYAN; end;
    T_STILETTI: begin Result := '\'; at := A_CYAN; end;
    T_KETJUT:   begin Result := 'ß'; at := A_CYAN; end;
    T_CAMEL:    begin Result := 'Θ'; at := A_CYAN; end;
    T_KONJAKKI: begin Result := 'Φ'; at := A_YELLOW; end;
    T_MONEY:    begin Result := '$'; at := A_GREEN; end;
    T_HINT:     begin Result := '¿'; at := A_MAGENTA; end;
    T_MILK:     begin Result := '⌂'; at := A_WHITE; end;
    T_CHOCO:    begin Result := '∞'; at := A_YELLOW; end;
    T_APPLE:    begin Result := 'o'; at := A_GREEN; end;
    T_SILAKKA:  begin Result := '¥'; at := A_CYAN; end;
    T_MAKKARA:  begin Result := '('; at := A_YELLOW; end;
    T_DOT:      begin Result := '.'; at := A_WHITE; end;
    T_EXIT:     begin Result := '>'; at := A_GREEN; end;
    T_WATER:    begin Result := '≈'; at := A_BLUE; end;
    T_POOP:     begin Result := 'æ'; at := A_DIM; end;
    T_POLICE1..T_POLICE5: begin Result := 'C'; at := A_BLUE; end;
    T_MUMMO:    begin Result := 'M'; at := A_MAGENTA; end;
    T_KAKE:     begin Result := 'K'; at := A_MAGENTA; end;
    T_VARAS:    begin Result := 'V'; at := A_RED; end;
    T_TEEKKARI: begin Result := 'T'; at := A_MAGENTA; end;
    T_PORTSARI: begin Result := 'Ω'; at := A_RED; end;
    T_NEEKERI:  begin Result := 'N'; at := A_MAGENTA; end;
    T_HINTTI:   begin Result := 'H'; at := A_MAGENTA; end;
    T_TROKARI:  begin Result := 'Q'; at := A_MAGENTA; end;
    T_PAPPI:    begin Result := 'R'; at := A_MAGENTA; end;
    T_HIPPI:    begin Result := 'I'; at := A_RED; end;
    T_PIRKKO:   begin Result := 'B'; at := A_RED; end;
    T_DOG:      begin Result := 'D'; at := A_RED; end;
    T_GORILLA:  begin Result := 'G'; at := A_RED; end;
    T_SKIN:     begin Result := 'O'; at := A_RED; end;
    T_SNAKE:    begin Result := 'S'; at := A_RED; end;
    T_AIDS:     begin Result := 'A'; at := A_RED; end;
    T_PUNK:     begin Result := 'P'; at := A_RED; end;
    T_KINGI:    begin Result := 'Ñ'; at := A_RED; end;
    T_JUPPI:    begin Result := 'J'; at := A_RED; end;
    T_KARKURI:  begin Result := 'E'; at := A_RED; end;
    T_GHOST:    begin Result := ' '; end;
    T_GRAVE:    begin Result := '±'; at := A_DIM; end;
    T_FENCE:    begin Result := '#'; at := A_YELLOW; end;
  else
    Result := ' ';
  end;
end;

function IsCreature(t: TTile): Boolean;
begin
  Result := (t >= T_POLICE1) and (t <= T_KARKURI) or (t = T_GHOST);
end;

procedure DrawFrame;
var c: Integer;
begin
  SPut(FRAME_L, FRAME_TOP, '╔', A_CYAN);
  for c := FRAME_L + 1 to FRAME_R - 1 do SPut(c, FRAME_TOP, '═', A_CYAN);
  SPut(FRAME_R, FRAME_TOP, '╗', A_CYAN);
  for c := FRAME_TOP + 1 to FRAME_BOT - 1 do
  begin
    SPut(FRAME_L, c, '║', A_CYAN);
    SPut(FRAME_R, c, '║', A_CYAN);
  end;
  SPut(FRAME_L, FRAME_BOT, '╚', A_CYAN);
  for c := FRAME_L + 1 to FRAME_R - 1 do SPut(c, FRAME_BOT, '═', A_CYAN);
  SPut(FRAME_R, FRAME_BOT, '╝', A_CYAN);
end;

procedure DrawCityscape;
{ building glyphs and names (map renderer file 0x3A80..0x404C) }
const B = A_CYAN; N = A_WHITE;
begin
  { top-left: ISKU / ALCO }
  SPut(20, 3, '▐', B); SPut(20, 4, '▐', B); SPut(20, 5, '▐', B);
  SPut(14, 6, '▀▀▒▒▀▀▀', B);
  if FlagAlco then SPut(15, 4, 'ALCO', N) else SPut(15, 4, 'ISKU', N);
  { top-middle: SAIRAALA / HOTELLI }
  SPut(34, 3, '█', B); SPut(34, 4, '█', B); SPut(34, 5, '█', B); SPut(34, 6, '█', B);
  SPut(55, 3, '█', B); SPut(55, 4, '█', B); SPut(55, 5, '█', B); SPut(55, 6, '█', B);
  SPut(35, 6, '▄▄▄▄▄▄▄▄▄┬┬▄▄▄▄▄▄▄▄▄', B);
  if HotelName then SPut(41, 4, 'HOTELLI', N) else SPut(41, 4, 'SAIRAALA', N);
  { bottom-left (DIVARI) }
  SPut(14, 19, '▀▀▀▒▀▀█', B);
  SPut(20, 20, '█', B); SPut(20, 21, '█', B); SPut(20, 22, '█', B);
  if FlagDivari then SPut(14, 21, 'DIVARI', N);
  { bottom-middle and bottom-right }
  SPut(43, 19, '█▀▀▀▒▀▀▀█', B);
  SPut(43, 20, '█', B); SPut(51, 20, '█', B);
  SPut(43, 21, '█', B); SPut(51, 21, '█', B);
  SPut(43, 22, '█', B); SPut(51, 22, '█', B);
  SPut(54, 19, '█▀▀▀▒▀▀▀█', B);
  SPut(54, 20, '█', B); SPut(62, 20, '█', B);
  SPut(54, 21, '█', B); SPut(62, 21, '█', B);
  SPut(54, 22, '█', B); SPut(62, 22, '█', B);
  if FlagPoliisi then
  begin
    SPut(44, 21, 'POLIISI', N);
    SPut(55, 21, 'VANKILA', N);
  end;
  { top-right alcove wall (flag [0x2E9]) }
  if FlagAlcove then
  begin
    SPut(76, 3, '▐', B); SPut(76, 4, '▐', B); SPut(76, 5, '▐', B);
  end;
end;

procedure DrawMap;
var
  x, y: Integer;
  t: TTile;
  g: string;
  at: Byte;
begin
  DrawFrame;
  SFill(MAP_X0, MAP_Y0, MAP_X1, MAP_Y1);
  if not Blind then
  begin
    for y := MAP_Y0 to MAP_Y1 do
      for x := MAP_X0 to MAP_X1 do
      begin
        t := Map[x, y];
        if t = T_WALL then Continue;
        g := TileGlyph(t, at);
        if (t = T_GHOST) and (Prom >= 2.0) then
        begin
          { "Aaveita et selvänä näe" -- drunk eyes see the ghosts }
          g := '¤'; at := A_DIM;
        end;
        if Friendly and IsCreature(t) and (t <> T_GHOST) then
          g := Chr(Ord('@') + Random(10) + 1);    { hallucinated letters }
        SPut(x, y, g, at);
      end;
    DrawCityscape;
  end;
  SPut(PX, PY, '@', A_WHITE);
  SFill(1, 19, 12, 19);
  SPut(1, 19, StateText, A_BOLD);
end;

procedure DrawStatus;
var s: string;
begin
  SFill(1, STAT_ROW, SCRW, STAT_ROW);
  SPut(1,  STAT_ROW, 'Taso: ' + IntToStr(Level));
  SPut(14, STAT_ROW, 'Mk: ' + IntToStr(Mk));
  SPut(27, STAT_ROW, 'Voima: ' + IntToStr(Round(Voima)));
  SPut(41, STAT_ROW, 'Kokemus: ' + IntToStr(Kokemus));
  SPut(57, STAT_ROW, 'Prom: ' + FormatFloat('0.0', Prom));
  s := Format('%5.0f', [Turn]);
  SPut(75, STAT_ROW, s);
end;

procedure FullRedraw;
begin
  DrawMap;
  DrawStatus;
  DrawMsg;
  if MsgLine = '' then
  begin
    CurCol := PX; CurRow := PY;
  end;
end;

{ ===========================================================================
  Map helpers
  =========================================================================== }
function InMap(x, y: Integer): Boolean;
begin
  Result := (x >= MAP_X0) and (x <= MAP_X1) and (y >= MAP_Y0) and (y <= MAP_Y1);
end;

{ The punk gang (0xA1AC..0xA476) is a "<"-shaped block of five tiles around
  the king Ñ at (KingX, KingY):   P P
                                    Ñ      (the mouth at KingX-1 is open)
                                  P P                                      }
const
  PUNK_DX: array[0..4] of Integer = (0, -1, 0, -1, 0);
  PUNK_DY: array[0..4] of Integer = (0, -1, -1, 1, 1);

function PunkCellOk(i: Integer): Boolean;
var x, y: Integer;
begin
  x := KingX + PUNK_DX[i]; y := KingY + PUNK_DY[i];
  Result := InMap(x, y) and not ((x = PX) and (y = PY));
end;

procedure PunkStamp;
var i: Integer;
begin
  for i := 0 to 4 do
    if PunkCellOk(i) then
    begin
      PunkUnder[i] := Map[KingX + PUNK_DX[i], KingY + PUNK_DY[i]];
      if PunkUnder[i] in [T_PUNK, T_KINGI] then PunkUnder[i] := T_FLOOR;
      if i = 0 then Map[KingX, KingY] := T_KINGI
      else Map[KingX + PUNK_DX[i], KingY + PUNK_DY[i]] := T_PUNK;
    end;
end;

procedure PunkErase;
{ The original wrote blanks over the gang's old cells, eating walls, dots and
  even the exit on its way.  We put back whatever was under it instead, so a
  passing gang cannot make the level unfinishable. }
var i, x, y: Integer;
begin
  for i := 0 to 4 do
  begin
    x := KingX + PUNK_DX[i]; y := KingY + PUNK_DY[i];
    if InMap(x, y) and (Map[x, y] in [T_PUNK, T_KINGI]) then
      Map[x, y] := PunkUnder[i];
  end;
end;

function Tile(x, y: Integer): TTile;
begin
  if InMap(x, y) then Result := Map[x, y] else Result := T_WALL;
end;

procedure SetTile(x, y: Integer; t: TTile);
begin
  if InMap(x, y) then Map[x, y] := t;
end;

function IsFree(x, y: Integer): Boolean;
{ empty floor and not the player's own cell }
begin
  Result := InMap(x, y) and (Map[x, y] = T_FLOOR) and not ((x = PX) and (y = PY));
end;

function FindTile(t: TTile; var fx, fy: Integer): Boolean;
var x, y: Integer;
begin
  for y := MAP_Y0 to MAP_Y1 do
    for x := MAP_X0 to MAP_X1 do
      if Map[x, y] = t then
      begin
        fx := x; fy := y; Result := True; Exit;
      end;
  Result := False;
end;

function Adjacent(x, y: Integer): Boolean;
begin
  Result := (Abs(x - PX) < 2) and (Abs(y - PY) < 2);
end;

function RandomCell(x0, xn, y0, yn: Integer; allowDot: Boolean;
  var rx, ry: Integer): Boolean;
{ random empty cell in [x0, x0+xn) x [y0, y0+yn) }
var tries: Integer;
begin
  tries := 2000;
  repeat
    rx := x0 + Random(xn);
    ry := y0 + Random(yn);
    Dec(tries);
    Result := IsFree(rx, ry) or (allowDot and InMap(rx, ry) and
              (Map[rx, ry] = T_DOT) and not ((rx = PX) and (ry = PY)));
  until Result or (tries <= 0);
end;

procedure Place(t: TTile);
{ placement helper 0x2DF3: x=23+Random(56), y=7+Random(12), retry until empty }
var x, y: Integer;
begin
  if RandomCell(PLACE_X0, 56, PLACE_Y0, 12, False, x, y) then
    Map[x, y] := t;
end;

function Min(a, b: Integer): Integer;
begin
  if a < b then Result := a else Result := b;
end;

function Max(a, b: Integer): Integer;
begin
  if a > b then Result := a else Result := b;
end;

function Sgn(v: Integer): Integer;
begin
  if v > 0 then Result := 1 else if v < 0 then Result := -1 else Result := 0;
end;

{ ===========================================================================
  Equipment / weapon level
  =========================================================================== }
procedure RecomputeWeapon;
{ weapon level [0x2F6]: Ketjut 5, Stiletti 3, Pamppu 2, +1 with Camel boots }
begin
  if Equip[E_KETJUT] <> '' then Weapon := 5
  else if Equip[E_STILETTI] <> '' then Weapon := 3
  else if Equip[E_PAMPPU] <> '' then Weapon := 2
  else Weapon := 0;
  if Equip[E_CAMEL] <> '' then Inc(Weapon);
end;

procedure KillBonus(cap: Integer);
{ a won fight may raise Kokemus [0x2EC] (only while it is below the
  creature's cap -- weak foes stop teaching you) and gives extra Voima }
begin
  if (Random(2) * Random(2) = 1) and (Kokemus < cap) then
  begin
    Inc(Kokemus);
    Voima := Voima + Random(Min(Level, 8)) + 1;
  end;
end;

{ ===========================================================================
  Level generation
  =========================================================================== }
procedure BuildWalls;
{ fixed wall template, identical on every level (180 cells) }
var x, y: Integer;
begin
  for y := 0 to SCRH do
    for x := 0 to SCRW + 1 do
      Map[x, y] := T_FLOOR;
  for y := 3 to 5 do Map[20, y] := T_WALL;
  for x := 14 to 20 do Map[x, 6] := T_WALL;
  for y := 3 to 6 do
    for x := 34 to 55 do Map[x, y] := T_WALL;
  for x := 14 to 20 do Map[x, 19] := T_WALL;
  for x := 43 to 51 do Map[x, 19] := T_WALL;
  for x := 54 to 62 do Map[x, 19] := T_WALL;
  for y := 20 to 22 do
  begin
    Map[20, y] := T_WALL;
    for x := 43 to 51 do Map[x, y] := T_WALL;
    for x := 54 to 62 do Map[x, y] := T_WALL;
  end;
end;

procedure ResetLevelState;
var i: Integer;
begin
  for i := 0 to T_MAXCODE do HP[i] := 0;
  PoliceActive := False;
  PortsariOn := False;
  PunksOn := False;
  Blind := False;
  DotsGot := 0;
  PX := START_X; PY := START_Y;
end;

procedure BuildLevel1;
{ level 1 is fixed (byte-identical in every run of the original) }
begin
  ResetLevelState;
  BuildWalls;
  Map[52, 10] := T_POLICE1;
  Map[59, 10] := T_KALJA;
  Map[77, 11] := T_MUMMO;
  Map[65, 13] := T_POLICE1 + 1;
  Map[61, 14] := T_HINT;
  Map[64, 14] := T_DOT;
  Map[26, 15] := T_MAKKARA;
  Map[46, 18] := T_POLICE1 + 2;
  Map[51, 18] := T_STILETTI;
  Map[EXIT_X, EXIT_Y] := T_EXIT;
  HP[T_POLICE1] := 3; HP[T_POLICE1 + 1] := 3; HP[T_POLICE1 + 2] := 3;
  HP[T_MUMMO] := 0;
  DotsNeeded := 1;
  FlagAlco := False; FlagDivari := False; FlagPoliisi := False;
  FlagGrave := False; FlagZoo := False; FlagAlcove := False;
  HotelName := False;
end;

procedure BuildGraveyard;
{ levels 13 and 31 (0x2F32): graves, a ghost and a priest }
var x, y: Integer;
begin
  for x := 28 to 72 do
    for y := 9 to 17 do
      if (Map[x, y] = T_FLOOR) and (x mod 3 = 1) and (y mod 2 = 1) and
         (Random(3) > 0) then
        Map[x, y] := T_GRAVE;
  Place(T_GHOST);
  HP[T_GHOST] := 10 + Random(8);
  Place(T_PAPPI);
  HP[T_PAPPI] := 3 + Random(Level);
end;

procedure BuildZoo;
{ levels 17 and 25 (0x308A): electric fences, an escaped gorilla, a hippie }
var x, y: Integer;
begin
  for x := 27 to 75 do
    for y := 8 to 17 do
      if (Map[x, y] = T_FLOOR) and (Random(9) = 0) then
        Map[x, y] := T_FENCE;
  Place(T_GORILLA);
  HP[T_GORILLA] := 10 + Random(7);
  Place(T_HIPPI);
  HP[T_HIPPI] := 7 + Random(7);
end;

procedure FixReachability;
{ make sure every dot and the exit can be reached from the start: graves
  and fences are permanent obstacles, so a dot boxed in by them is moved }
var
  seen: array[0..SCRW+1, 0..SCRH] of Boolean;
  qx, qy: array[0..2000] of Integer;
  head, tail, x, y, dx, dy, nx, ny, rx, ry: Integer;
  t: TTile;
begin
  FillChar(seen, SizeOf(seen), 0);
  head := 0; tail := 0;
  qx[tail] := START_X; qy[tail] := START_Y; Inc(tail);
  seen[START_X, START_Y] := True;
  while head < tail do
  begin
    x := qx[head]; y := qy[head]; Inc(head);
    for dx := -1 to 1 do
      for dy := -1 to 1 do
      begin
        nx := x + dx; ny := y + dy;
        if not InMap(nx, ny) or seen[nx, ny] then Continue;
        t := Map[nx, ny];
        if (t = T_WALL) or (t = T_GRAVE) or (t = T_FENCE) then Continue;
        seen[nx, ny] := True;
        qx[tail] := nx; qy[tail] := ny; Inc(tail);
      end;
  end;
  for y := MAP_Y0 to MAP_Y1 do
    for x := MAP_X0 to MAP_X1 do
      if (Map[x, y] = T_DOT) and not seen[x, y] then
      begin
        Map[x, y] := T_FLOOR;
        repeat
          RandomCell(PLACE_X0, 56, PLACE_Y0, 12, False, rx, ry);
        until seen[rx, ry];
        Map[rx, ry] := T_DOT;
      end;
end;

procedure BuildLevel;
{ level generator (file 0x4216..0x4D20). Called after Level was raised. }
var i, x, y: Integer;
begin
  if Level = 1 then begin BuildLevel1; Exit; end;
  ResetLevelState;
  BuildWalls;
  Map[EXIT_X, EXIT_Y] := T_EXIT;

  FlagGrave := (Level = 13) or (Level = 31);
  FlagZoo   := (Level = 17) or (Level = 25);
  FlagAlco    := Random(100) + Level > 90;
  FlagDivari  := Random(100) - Level < 0;
  FlagPoliisi := (Random(Level) > 2) and not FlagGrave and not FlagZoo;
  FlagAlcove  := Random(Level) > 3;
  HotelName   := Random(2) = 1;
  if FlagAlcove then
    for y := 3 to 5 do Map[76, y] := T_WALL;

  if FlagGrave then BuildGraveyard;
  if FlagZoo then BuildZoo;
  if (Level >= MAX_LEVEL) and (Equip[E_APY] <> '') then
  begin
    { the cognac (0x322F) + a guarding ghost. The original tests
      "Level >= 50 and slot 8 = 'Äpy'" (0x449D), but Äpy is stored in
      slot 10 (shown as item 8), so the test could never pass and v1.17
      had no cognac. Here the intended rule is used: the cognac is on
      level 50 (or deeper) when you carry Äpy bought from a Teekkari.
      Like 0x322F it does not check whether you already hold the cognac. }
    Map[KONJAKKI_X, KONJAKKI_Y] := T_KONJAKKI;
    Place(T_GHOST);
    HP[T_GHOST] := 11 + Random(10);
  end;

  { police: slots $1E .. $1E+Random(5) }
  for i := T_POLICE1 to T_POLICE1 + Random(5) do
  begin
    Place(i);
    HP[i] := 2 + Min(Random(Level) + 1, 18);
  end;
  Place(T_MUMMO);
  HP[T_MUMMO] := Min(Random(Level) - 1, 10);

  { dots: one per level number }
  DotsNeeded := Level;
  for i := 1 to Level do Place(T_DOT);

  { items }
  if Random(10) = 9 then Place(T_MONEY);
  if (Random(21) = 6) and (Equip[E_CAMEL] = '') then Place(T_CAMEL);
  if Random(10) > 7 then Place(T_LONKKA);
  if Random(10) > 7 then Place(T_HINT);
  if Random(10) > 6 then Place(T_KALJA);
  if Random(20) = 6 then Place(T_MILK);
  if Random(30) = 21 then Place(T_CHOCO);
  if Random(20) = 11 then Place(T_APPLE);
  if Random(10) = 6 then Place(T_SILAKKA);
  if Random(10) = 9 then Place(T_MAKKARA);
  if Random(10) = 5 then Place(T_POOP);
  if Random(20) = 19 then Place(T_STILETTI);
  if (Equip[E_HAALARI] = '') and (Random(21) = 20) then Place(T_HAALARI);
  if (Random(30) = 19) and (Equip[E_KETJUT] = '') then Place(T_KETJUT);
  { a puddle now and then ("Puli juo lammikon kuiviin!") }
  if Random(6) = 0 then
  begin
    if RandomCell(PLACE_X0, 50, PLACE_Y0, 10, False, x, y) then
      for i := 0 to Random(3) + 1 do
        if IsFree(x + i, y) then Map[x + i, y] := T_WATER;
  end;

  { creatures }
  if (Random(10) = 1) and (Level > 1) then
  begin
    { punk gang around (19,12), the king in the middle }
    KingX := 19; KingY := 12;
    PunkStamp;
    HP[T_KINGI] := Level - Random(Level) + Random(10);
    PunksOn := True;
  end;
  if (Random(10) = 9) and (Level > 7) then
  begin
    Place(T_SKIN);
    HP[T_SKIN] := 4 + Random(10 + Level div 2);
  end;
  if not PunksOn and (Level > 1) and (Random(10) > 7) then
    Map[16, 12] := T_KAKE;
  if (Level > 1) and (Random(100) > 77) then
  begin
    Place(T_PIRKKO);
    HP[T_PIRKKO] := 0;          { banked dots ("life") }
  end;
  if (Random(10) = 4) and (Level > 3) then
  begin
    Place(T_SNAKE);
    HP[T_SNAKE] := 4 + Random(Level);
  end;
  if (Level > 3) and (Random(7) = 4) then
  begin
    Place(T_AIDS);
    HP[T_AIDS] := Min(Level, 8);
  end;
  if (Level > 1) and (Random(100) < 23) and not FindTile(T_PIRKKO, x, y) then
  begin
    Map[PORTSARI_X, PORTSARI_Y] := T_PORTSARI;
    HP[T_PORTSARI] := Min(Level, 9);
    PortsariOn := True;
  end;
  if (Mk > 0) and (Level > 1) and (Random(10) > 7) then
  begin
    Place(T_VARAS);
    HP[T_VARAS] := Min(Level, 13);
  end;
  if (Mk > 30) and (Level > 2) and (Random(5) = 1) then Place(T_TEEKKARI);
  if (Level > 2) and (Random(5) = 3) then
  begin
    Place(T_DOG);
    HP[T_DOG] := Max(Min(Trunc(Voima / 2), 8), 2);
  end;
  if (Level > 3) and (Random(16) > 10) then Place(T_NEEKERI);
  if (Level > 4) and (Random(18) > 12) then Place(T_HINTTI);
  if (Level > 10) and (Random(18) > 13) and not FindTile(T_PAPPI, x, y) then
  begin
    Place(T_PAPPI);
    HP[T_PAPPI] := Min(Level - Random(Level), 6);
  end;
  if (Level > 2) and (Random(7) = 6) and not FindTile(T_HIPPI, x, y) then
  begin
    Place(T_HIPPI);
    HP[T_HIPPI] := Max(Min(Level - Random(Level), 8), 1);
  end;
  if (Level > 3) and (Mk > 50) and (Random(21) = 13) then Place(T_TROKARI);
  if Random(Level) > 4 then
  begin
    Place(T_JUPPI);
    HP[T_JUPPI] := 6 + Random(Level div 2 + 1);
  end;
  if FlagPoliisi and (Level > 5) and (Random(4) = 1) then
  begin
    Place(T_KARKURI);
    HP[T_KARKURI] := 5 + Min(Random(Level), 12);
  end;

  FixReachability;
  { the exit opens when every dot placed here has been eaten }
  DotsNeeded := 0;
  for y := MAP_Y0 to MAP_Y1 do
    for x := MAP_X0 to MAP_X1 do
      if Map[x, y] = T_DOT then Inc(DotsNeeded);
end;

procedure NextLevel;
{ level init (file 0x4216..0x4412): Level+1, Kokemus+1, Voima+1 }
begin
  Inc(Level);
  Score := Score + 1;
  Voima := Voima + 1;
  if Friendly and (FriendlyTurns <= 0) then Friendly := False;
  BuildLevel;
end;

{ ===========================================================================
  Death
  =========================================================================== }
procedure Die(const cause: string);
begin
  if Dead then Exit;
  Dead := True;
  DeathCause := cause;
end;

procedure CheckDeath;
begin
  if (not Dead) and (Voima < 0) then
    Die('Voimasi ehtyvät, lyhistyt maahan ...');
end;

{ ===========================================================================
  Hint pager and help files
  =========================================================================== }
function ReadTip: string;
var L: TStringList; i: Integer;
begin
  L := TStringList.Create;
  try
    LoadLines('SPU.TIP', L);
    for i := L.Count - 1 downto 0 do
      if Trim(L[i]) = '' then L.Delete(i);
    if L.Count = 0 then Result := 'Kippis!'
    else Result := L[Random(L.Count)];
  finally
    L.Free;
  end;
end;

procedure HintPager;
{ two-page pager (0x5186) }
begin
  SetMsg('Ruumiisi alkaa vavahdella ja kuulet taivaallisen äänen!');
  DrawMsg;
  PressAny;
  SetMsg('Se sanoo: ' + ReadTip);
  DrawMsg;
  PressAny;
  SetMsg('');
end;

procedure ShowPagedText(L: TStrings);
var i, row: Integer;
begin
  i := 0;
  while i < L.Count do
  begin
    SClear;
    row := 1;
    while (i < L.Count) and (row <= SCRH - 1) do
    begin
      if Pos('<lis', L[i]) > 0 then
      begin
        Inc(i);
        Break;
      end;
      SPut(1, row, Copy(L[i], 1, 400));
      Inc(row); Inc(i);
    end;
    SPut(68, SCRH, '<lisää>', A_BOLD);
    CurRow := SCRH; CurCol := 76;
    if ReadKey in [#27, 'q'] then Break;
  end;
  SClear;
end;

procedure ShowHelp;
var L: TStringList;
begin
  L := TStringList.Create;
  try
    if not LoadLines('SPU.HLP', L) then L.Add('SPU.HLP puuttuu.');
    L.Add('<lisää>');
    L.Add('');
    L.Add('  KOMENNOT:');
    L.Add('');
    L.Add('  h j k l y u b n   liiku (nuolinäppäimetkin käyvät)');
    L.Add('  H J K L Y U B N   juokse, kunnes jokin estää');
    L.Add('  q  juo            e  syö            t  heitä');
    L.Add('  i  inventaario    .  odota          >  seuraavalle tasolle');
    L.Add('  ?  ohjeet         /  sepustus (mikä merkki on mitä)');
    L.Add('  v  versio         Q  luovuta');
    L.Add('');
    L.Add('  Versiosta 2.24 lisätyt:');
    L.Add('  S  talletus (peli jatkuu seuraavalla käynnistyskerralla)');
    L.Add('  p  viimeisin viesti   c  kello   ^L  piirrä ruutu uudelleen');
    L.Add('');
    L.Add('  Ovet: ALCO (17,7) myy juomia, DIVARI (17,18) ostaa tavaroita.');
    ShowPagedText(L);
  finally
    L.Free;
  end;
  ForceRedraw := True;
end;

procedure Sepustus;
{ '/' : "Sepusta mistä >" -- explain a map glyph from SPU.SPE }
var
  L: TStringList;
  c: Char;
  i: Integer;
  found: string;
begin
  c := AskKey('Sepusta mistä >');
  found := '';
  L := TStringList.Create;
  try
    LoadLines('SPU.SPE', L);
    for i := 0 to L.Count - 1 do
      if (Length(L[i]) > 0) and (L[i][1] = c) then
      begin
        found := L[i];
        Break;
      end;
  finally
    L.Free;
  end;
  if found = '' then SetMsg('Eihän tommosia olekaan!')
  else SetMsg(Copy(found, 1, 300));
end;

{ ===========================================================================
  High scores (SPU.TOP, 20 records x 28 bytes)
  The score is the 6-byte Turbo Pascal real at r[10..15] (Kokemus + 1).
  =========================================================================== }
const
  TOP_FILE  = 'SPU.TOP';
  TOP_COUNT = 20;
  R_REAL    = 10;
  R_LEVEL   = 16;
  { v2.24 style cause of death.  Bytes 18..27 of a v1.17 record are unused
    (the DOS program leaves garbage there), so the port marks its records
    with R_CMARK = $C5 and stores an index into CAUSES at R_CAUSE.  The file
    stays readable by the DOS version. }
  R_CMARK   = 18;
  R_CAUSE   = 19;
  CAUSE_MARK = $C5;
  CAUSE_WON  = 0;
  CAUSES: array[0..31] of string = (
    '', 'tuntematon', 'itsemurha', 'krapula', 'myrkytys', 'hukkuminen',
    'kallonmurtuma', 'mummo', 'väkivalta', 'neekeri', 'punkkarit', 'ilkeys',
    'vilustuminen', 'poliisi', 'rakkikoira', 'haamu', 'taskuvaras', 'murha',
    'Kake', 'hinttailu', 'masinol', 'juppi', 'piikki', 'käärme', 'gorilla',
    'AIDS', 'alkoholimyrkytys', '', '', '', '', '');

type
  TRec = array[0..27] of Byte;
  TSlots = array[1..TOP_COUNT] of TRec;

function TPRealDecode(const r: TRec; at: Integer): Double;
var
  e, i: Integer;
  m: QWord;
  neg: Boolean;
begin
  e := r[at];
  if e = 0 then begin Result := 0; Exit; end;
  m := 0;
  for i := 5 downto 1 do m := (m shl 8) or r[at + i];
  neg := (m and (QWord(1) shl 39)) <> 0;
  m := m and ((QWord(1) shl 39) - 1);
  Result := (1 + m / 549755813888.0) * Exp((e - 129) * Ln(2));
  if neg then Result := -Result;
end;

procedure TPRealEncode(var r: TRec; at: Integer; v: Double);
var
  e, i: Integer;
  m: QWord;
  neg: Boolean;
  f: Double;
begin
  for i := 0 to 5 do r[at + i] := 0;
  if v = 0 then Exit;
  neg := v < 0;
  f := Abs(v);
  e := 0;
  while f >= 2 do begin f := f / 2; Inc(e); end;
  while f < 1 do begin f := f * 2; Dec(e); end;
  m := Round((f - 1) * 549755813888.0);
  if m >= (QWord(1) shl 39) then begin m := 0; Inc(e); end;
  if neg then m := m or (QWord(1) shl 39);
  r[at] := Byte(e + 129);
  for i := 1 to 5 do
  begin
    r[at + i] := m and $FF;
    m := m shr 8;
  end;
end;

function RecScore(const r: TRec): Integer;
begin
  Result := Trunc(TPRealDecode(r, R_REAL) + 0.0001);
end;

function RecLevel(const r: TRec): Integer;
begin
  Result := Integer(r[R_LEVEL]) or (Integer(r[R_LEVEL + 1]) * 256);
end;

function RecName(const r: TRec): string;
var ln: Integer;
begin
  ln := r[0];
  if ln > 9 then ln := 9;
  SetLength(Result, ln);
  if ln > 0 then Move(r[1], Result[1], ln);
  Result := CP437ToUTF8(Result);
end;

function RecCause(const r: TRec): string;
{ '' = no cause stored (old/DOS record), '*' = won the game }
begin
  Result := '';
  if (r[R_CMARK] = CAUSE_MARK) and (r[R_CAUSE] <= High(CAUSES)) then
  begin
    if r[R_CAUSE] = CAUSE_WON then Result := '*'
    else Result := CAUSES[r[R_CAUSE]];
  end;
end;

function RecLine(const r: TRec): string;
var c: string;
begin
  c := RecCause(r);
  if c = '*' then
    Result := RecName(r) + ' selvisi tasolta ' + IntToStr(RecLevel(r)) +
              ' konjakin kera!'
  else if c = '' then
    Result := RecName(r) + ' menehtyi tasolle ' + IntToStr(RecLevel(r))
  else
    Result := RecName(r) + ' kuoli tasolle ' + IntToStr(RecLevel(r)) +
              ', kuolinsyy: ' + c + '.';
end;

function SameRec(const a, b: TRec): Boolean;
var i: Integer;
begin
  Result := True;
  for i := 0 to 4 do if a[i] <> b[i] then Result := False;
  for i := 10 to 15 do if a[i] <> b[i] then Result := False;
end;

function TopPath: string;
begin
  Result := FindData(TOP_FILE);
  if Result = '' then Result := IncludeTrailingPathDelimiter(GetCurrentDir) + TOP_FILE;
end;

function LoadTop(var Slots: TSlots): Integer;
var
  fs: TFileStream;
  i: Integer;
begin
  Result := 0;
  for i := 1 to TOP_COUNT do FillChar(Slots[i], SizeOf(TRec), 0);
  if not FileExists(TopPath) then Exit;
  try
    fs := TFileStream.Create(TopPath, fmOpenRead or fmShareDenyNone);
    try
      while (Result < TOP_COUNT) and (fs.Position + SizeOf(TRec) <= fs.Size) do
      begin
        Inc(Result);
        fs.ReadBuffer(Slots[Result], SizeOf(TRec));
      end;
    finally
      fs.Free;
    end;
  except
    Result := 0;
  end;
end;

procedure SaveTop(var Slots: TSlots; count: Integer);
var
  fs: TFileStream;
  i: Integer;
begin
  try
    fs := TFileStream.Create(TopPath, fmCreate);
    try
      for i := 1 to count do fs.WriteBuffer(Slots[i], SizeOf(TRec));
    finally
      fs.Free;
    end;
  except
    { read-only location: the table is still shown }
  end;
end;

function UTF8ToCP437Name(const s: string): string;
{ names are stored as CP437 bytes like the original; non-ASCII Finnish
  letters are mapped back, anything else becomes '?' }
var i, n: Integer; c: string;
begin
  Result := '';
  i := 1;
  while i <= Length(s) do
  begin
    n := U8Len(s[i]);
    c := Copy(s, i, n);
    if n = 1 then Result := Result + c
    else if c = 'ä' then Result := Result + #$84
    else if c = 'ö' then Result := Result + #$94
    else if c = 'Ä' then Result := Result + #$8E
    else if c = 'Ö' then Result := Result + #$99
    else if c = 'å' then Result := Result + #$86
    else if c = 'Å' then Result := Result + #$8F
    else Result := Result + '?';
    Inc(i, n);
  end;
end;

function MakeRec: TRec;
var nm: string; i: Integer;
begin
  FillChar(Result, SizeOf(TRec), 0);
  nm := Copy(UTF8ToCP437Name(PlayerName), 1, 9);
  Result[0] := Length(nm);
  if Length(nm) > 0 then Move(nm[1], Result[1], Length(nm));
  if Score + 1 > 0 then TPRealEncode(Result, R_REAL, Trunc(Score) + 1);
  Result[R_LEVEL]     := Byte(Level and $FF);
  Result[R_LEVEL + 1] := Byte((Level shr 8) and $FF);
  Result[R_CMARK] := CAUSE_MARK;
  if Won then Result[R_CAUSE] := CAUSE_WON
  else
  begin
    Result[R_CAUSE] := 1;
    for i := 1 to High(CAUSES) do
      if (CAUSES[i] <> '') and (CAUSES[i] = Killer) then Result[R_CAUSE] := i;
  end;
end;

procedure ShowHiscore(const NewRec: TRec);
var
  Slots: TSlots;
  n, i, pos, count, upper: Integer;
begin
  n := LoadTop(Slots);
  pos := 1;
  while (pos <= TOP_COUNT) and (pos <= n) and
        (RecScore(NewRec) <= RecScore(Slots[pos])) do
    Inc(pos);
  if pos <= TOP_COUNT then
  begin
    upper := Min(n + 1, TOP_COUNT);
    for i := upper downto pos + 1 do Slots[i] := Slots[i - 1];
    Slots[pos] := NewRec;
    count := upper;
  end
  else
    count := TOP_COUNT;
  SaveTop(Slots, count);

  SClear;
  SPut(30, 1, '20 PARASTA SPURGUA :', A_BOLD);
  for i := 1 to count do
  begin
    SPut(2, i + 2, IntToStr(i) + '.');
    SPut(7, i + 2, Format('%6d', [RecScore(Slots[i])]));
    if SameRec(Slots[i], NewRec) then
    begin
      SPut(1, i + 2, '>', A_YELLOW);   { this game (was "*** TEIT SEN ! ***") }
      SPut(20, i + 2, RecLine(Slots[i]), A_YELLOW);
    end
    else
      SPut(20, i + 2, RecLine(Slots[i]));
  end;
  if pos > TOP_COUNT then
  begin
    SPut(7, 23, Format('%6d', [RecScore(NewRec)]));
    SPut(20, 23, RecLine(NewRec), A_YELLOW);
  end;
  SPut(30, 25, 'SpurguX Vs. 1.17, (C) 1987 P.N.');
  CurRow := 25; CurCol := 1;
  ReadKeyAfterPause(1500);
end;

{ ===========================================================================
  Save / load (v2.24 "S = talletus")
  =========================================================================== }
const
  SAVE_FILE = 'SPU.SAV';
  SAVE_MAGIC = 'SPURGUX117SAVE4';

function SavePath: string;
begin
  Result := ExtractFilePath(TopPath) + SAVE_FILE;
end;

procedure WriteStr(fs: TStream; const s: string);
var n: Integer;
begin
  n := Length(s);
  fs.WriteBuffer(n, SizeOf(n));
  if n > 0 then fs.WriteBuffer(s[1], n);
end;

function ReadStr(fs: TStream): string;
var n: Integer;
begin
  fs.ReadBuffer(n, SizeOf(n));
  if (n < 0) or (n > 1000) then raise Exception.Create('bad save');
  SetLength(Result, n);
  if n > 0 then fs.ReadBuffer(Result[1], n);
end;

procedure SaveGame;
var fs: TFileStream; i: Integer;
begin
  fs := TFileStream.Create(SavePath, fmCreate);
  try
    WriteStr(fs, SAVE_MAGIC);
    WriteStr(fs, PlayerName);
    fs.WriteBuffer(Map, SizeOf(Map));
    fs.WriteBuffer(PX, SizeOf(PX)); fs.WriteBuffer(PY, SizeOf(PY));
    fs.WriteBuffer(Level, SizeOf(Level)); fs.WriteBuffer(Mk, SizeOf(Mk));
    fs.WriteBuffer(Voima, SizeOf(Voima)); fs.WriteBuffer(Prom, SizeOf(Prom));
    fs.WriteBuffer(Turn, SizeOf(Turn)); fs.WriteBuffer(Score, SizeOf(Score));
    fs.WriteBuffer(DotsNeeded, SizeOf(DotsNeeded));
    fs.WriteBuffer(DotsGot, SizeOf(DotsGot));
    fs.WriteBuffer(InvKalja, SizeOf(InvKalja));
    fs.WriteBuffer(InvLonkka, SizeOf(InvLonkka));
    fs.WriteBuffer(InvKiekura, SizeOf(InvKiekura));
    for i := 0 to 11 do WriteStr(fs, Equip[i]);
    fs.WriteBuffer(Kokemus, SizeOf(Kokemus));
    fs.WriteBuffer(HP, SizeOf(HP));
    fs.WriteBuffer(PoliceActive, 1); fs.WriteBuffer(Friendly, 1);
    fs.WriteBuffer(FriendlyTurns, SizeOf(FriendlyTurns));
    fs.WriteBuffer(Aids, 1); fs.WriteBuffer(Blind, 1);
    fs.WriteBuffer(PortsariOn, 1); fs.WriteBuffer(PunksOn, 1);
    fs.WriteBuffer(KingX, SizeOf(KingX)); fs.WriteBuffer(KingY, SizeOf(KingY));
    fs.WriteBuffer(PunkUnder, SizeOf(PunkUnder));
    fs.WriteBuffer(FlagAlco, 1); fs.WriteBuffer(FlagDivari, 1);
    fs.WriteBuffer(FlagPoliisi, 1); fs.WriteBuffer(FlagGrave, 1);
    fs.WriteBuffer(FlagZoo, 1); fs.WriteBuffer(FlagAlcove, 1);
    fs.WriteBuffer(HotelName, 1);
    WriteStr(fs, Killer);
  finally
    fs.Free;
  end;
end;

function LoadGame: Boolean;
var fs: TFileStream; i: Integer;
begin
  Result := False;
  try
    fs := TFileStream.Create(SavePath, fmOpenRead or fmShareDenyNone);
    try
      if ReadStr(fs) <> SAVE_MAGIC then Exit;
      PlayerName := ReadStr(fs);
      fs.ReadBuffer(Map, SizeOf(Map));
      fs.ReadBuffer(PX, SizeOf(PX)); fs.ReadBuffer(PY, SizeOf(PY));
      fs.ReadBuffer(Level, SizeOf(Level)); fs.ReadBuffer(Mk, SizeOf(Mk));
      fs.ReadBuffer(Voima, SizeOf(Voima)); fs.ReadBuffer(Prom, SizeOf(Prom));
      fs.ReadBuffer(Turn, SizeOf(Turn)); fs.ReadBuffer(Score, SizeOf(Score));
      fs.ReadBuffer(DotsNeeded, SizeOf(DotsNeeded));
      fs.ReadBuffer(DotsGot, SizeOf(DotsGot));
      fs.ReadBuffer(InvKalja, SizeOf(InvKalja));
      fs.ReadBuffer(InvLonkka, SizeOf(InvLonkka));
      fs.ReadBuffer(InvKiekura, SizeOf(InvKiekura));
      for i := 0 to 11 do Equip[i] := ReadStr(fs);
      fs.ReadBuffer(Kokemus, SizeOf(Kokemus));
      fs.ReadBuffer(HP, SizeOf(HP));
      fs.ReadBuffer(PoliceActive, 1); fs.ReadBuffer(Friendly, 1);
      fs.ReadBuffer(FriendlyTurns, SizeOf(FriendlyTurns));
      fs.ReadBuffer(Aids, 1); fs.ReadBuffer(Blind, 1);
      fs.ReadBuffer(PortsariOn, 1); fs.ReadBuffer(PunksOn, 1);
      fs.ReadBuffer(KingX, SizeOf(KingX)); fs.ReadBuffer(KingY, SizeOf(KingY));
      fs.ReadBuffer(PunkUnder, SizeOf(PunkUnder));
      fs.ReadBuffer(FlagAlco, 1); fs.ReadBuffer(FlagDivari, 1);
      fs.ReadBuffer(FlagPoliisi, 1); fs.ReadBuffer(FlagGrave, 1);
      fs.ReadBuffer(FlagZoo, 1); fs.ReadBuffer(FlagAlcove, 1);
      fs.ReadBuffer(HotelName, 1);
      Killer := ReadStr(fs);
      Result := True;
    finally
      fs.Free;
    end;
  except
    Result := False;
  end;
  RecomputeWeapon;
end;

{ ===========================================================================
  Inventory, drinking, eating
  =========================================================================== }
procedure DrawInventory;
{ inventory panel (0x501C): drink counts, then entries 3..11 as 1..9 }
var i: Integer;
begin
  SFill(1, 2, 12, 15);
  SPut(2, 2, 'Kaljaa   ' + IntToStr(InvKalja));
  SPut(1, 3, ' Lonkkaa  ' + IntToStr(InvLonkka));
  SPut(1, 4, ' Kiekuraa ' + IntToStr(InvKiekura));
  for i := 3 to 11 do
    SPut(1, i + 2, IntToStr(i - 2) + '. ' + Equip[i]);
end;

procedure DoInventory;
begin
  DrawInventory;
  SPut(1, 15, 'PAINA Enter', A_BOLD);
  CurRow := 15; CurCol := 12;
  ReadKey;
  SFill(1, 2, 12, 15);
end;

procedure DoDrink;
{ consume routine 0x7E50: Prom += 0.1 + d/10 (kalja 0.2, lonkka 0.3) }
var ch: Char;
begin
  if InvKalja + InvLonkka = 0 then
  begin
    SetMsg('Valitan, sulla on juomiset lopussa.');
    Exit;
  end;
  if InvKalja = 0 then ch := '2'
  else if InvLonkka = 0 then ch := '1'
  else
    repeat
      ch := AskKey('Kumpaako kittaat, kaljaa vai lonkkua (1/2) >');
    until ch in ['0', '1', '2', #27];
  SetMsg('');
  case ch of
    '1': begin Dec(InvKalja); Prom := Prom + 0.2; SetMsg('Gluglugluglugluglu...'); end;
    '2': begin Dec(InvLonkka); Prom := Prom + 0.3; SetMsg('Gluglugluglugluglu...'); end;
  end;
end;

procedure DoEat;
begin
  if InvKiekura = 0 then
    SetMsg('Et omista mitään ruoaksi kelpaavaa.')
  else
  begin
    Dec(InvKiekura);
    Voima := Voima + 1;
    SetMsg('Röyh...');
  end;
end;

{ ===========================================================================
  Shops and building doors
  =========================================================================== }
procedure AlcoShop;
{ door (17,7) under the top-left building (0x7774..0x794D) }
var ch: Char; price: Integer;
begin
  if Prom > 1.0 then
  begin
    SetMsg('Teille ei myydä, te olette humalassa!');
    Exit;
  end;
  if (Mk < 15) or not FlagAlco then
  begin
    SetMsg('Putiikki vaikuttaa suljetulta.');
    Exit;
  end;
  repeat
    ch := AskKey('Ja mitähan herralle saisi olla? (1=kalja/2=lonkka) >');
  until ch in ['0', '1', '2', #27];
  if not (ch in ['1', '2']) then begin SetMsg(''); Exit; end;
  price := 5 * (Ord(ch) - Ord('0') + 1);
  if ch = '1' then Inc(InvKalja) else Inc(InvLonkka);
  Dec(Mk, price);
  SetMsg(IntToStr(price) + ' mk, kiitos.');
end;

procedure Divari;
{ pawn shop door (17,18) (0x7993..0x7CC6) }
var ch: Char; e, price: Integer; name: string;
begin
  DrawInventory;
  repeat
    ch := AskKey('Tervetuloa, ' + PlayerName + ', mitäs myisit? >');
  until ch in ['0'..'9', #27];
  SFill(1, 2, 12, 15);
  if not (ch in ['1'..'9']) then begin SetMsg(''); Exit; end;
  e := Ord(ch) - Ord('0') + 2;
  if Equip[e] = '' then begin SetMsg(''); Exit; end;
  price := 0;
  case e of
    E_HAALARI:  begin begin Killer := 'tuntematon'; Voima := Voima - 2; end; price := 100; Score := Score - Trunc(Score / 10); end;
    E_PAMPPU:   price := Random(50);
    E_STILETTI: price := Random(80);
    E_KETJUT:   price := Random(100);
    E_CAMEL:    begin price := Random(101); begin Killer := 'tuntematon'; Voima := Voima - 1; end; end;
    E_APY:      price := 10;
    E_HENNESSY: price := 250;
  end;
  name := Equip[e];
  SetMsg(name + ' meni ' + IntToStr(price) + ' markalla.');
  DrawMsg;
  PressAny;
  SetMsg('Näkemiin kiitos!');
  Equip[e] := '';
  Inc(Mk, price);
  RecomputeWeapon;
end;

procedure BumpWall;
{ building door handler (0x7774..0x7E0E); bumping costs 0.1 Voima }
begin
  if (PY = 7) and (PX = 17) then AlcoShop
  else if FlagAlco and (PY = 7) and (PX = 16) then SetMsg('Toinen ovi, tollo!')
  else if FlagDivari and (PY = 18) and (PX = 17) then Divari
  else if (PY = 7) and ((PX = 44) or (PX = 45)) then
  begin
    { v2.24 sold an AIDS vaccine (69 mk); here the hospital treats sober
      paying customers }
    if Aids and not HotelName and (Prom <= 1.0) and (Mk >= 69) then
    begin
      Dec(Mk, 69);
      Aids := False;
      StateText := ST_BLANK;
      SetMsg('Saat lääkekuurin, 69 mk. AIDS ei enää paina.');
    end
    else SetMsg('Emme ota juoppoja!');
  end
  else if (PY = 18) and (PX = 47) then SetMsg('Ei ketään kotona.')
  else if (PY = 18) and (PX = 58) then SetMsg('Valitan, ei vapaita huoneita.')
  else
  begin
    SetMsg('Isket päätäsi seinään, mielenkiintoista.');
    Killer := 'kallonmurtuma';
    Voima := Voima - 0.1;
  end;
end;

{ ===========================================================================
  Fighting (player walks into a creature, turn processor 0x607B..0x7774)
  =========================================================================== }
procedure TeleportPirkko(bx, by: Integer);
var rx, ry: Integer;
begin
  if RandomCell(22, 58, 3, 20, False, rx, ry) then
  begin
    SetTile(bx, by, T_FLOOR);
    SetTile(rx, ry, T_PIRKKO);
  end;
end;

procedure PunkBlackout;
{ the punk king falls (0x5195..0x5472): the gang leaves, loot drops }
var x, y: Integer;
begin
  SetMsg('Hän sammuu!');
  if Voima > 5 then Msg('Pojat lähtevät nukkumaan.')
  else Msg('Punkkarit luikkivat pakoon.');
  PunkErase;
  for y := MAP_Y0 to MAP_Y1 do
    for x := MAP_X0 to MAP_X1 do
      if (Map[x, y] = T_PUNK) or (Map[x, y] = T_KINGI) then Map[x, y] := T_FLOOR;
  PunksOn := False;
  Score := Score + 2;
  if IsFree(PX + 1, PY) then
  begin
    if Equip[E_KETJUT] = '' then Map[PX + 1, PY] := T_KETJUT
    else if Random(2) = 1 then Map[PX + 1, PY] := T_MONEY;
  end;
end;

procedure Attack(x, y: Integer);
var
  t: TTile;
  d, n: Integer;
begin
  t := Map[x, y];
  d := 1 + Weapon;
  case t of
    T_POLICE1..T_POLICE5:
      begin
        PoliceActive := True;
        SetMsg('Huitaiset skoudea!');
        Dec(HP[t], d);
        if (Prom > 0.5) and (Prom < 1.5) then Dec(HP[t]);   { just tipsy enough }
        if HP[t] < 1 then
        begin
          Msg('Hän väsähtää!');
          KillBonus(6);
          Score := Score + 1;
          if Equip[E_PAMPPU] = '' then SetTile(x, y, T_PAMPPU)
          else if Random(10) = 9 then SetTile(x, y, T_MONEY)
          else SetTile(x, y, T_FLOOR);
        end;
      end;
    T_MUMMO:
      if HP[t] < d then
      begin
        SetMsg('Isket mummon kumoon!');
        PoliceActive := True;
        SetTile(x, y, T_MONEY);
      end
      else
      begin
        SetMsg('Eläkeläinen lyö sinua käsilaukullaan!');
        Dec(HP[t], d);
        Killer := 'mummo';
        Voima := Voima - 1;
      end;
    T_KAKE:
      begin
        SetMsg('Huitaiset Kakea.');
        if InvKalja > 0 then
        begin
          Msg('Hän pöllii sinulta yhden kaljan.');
          Dec(InvKalja);
        end;
        SetTile(x, y, T_FLOOR);
      end;
    T_VARAS:
      begin
        SetMsg('Lyöt varasta.');
        Dec(HP[t], d);
        if HP[t] < 1 then
        begin
          Msg('Varas tuupertuu maahan.');
          Voima := Voima + 1; Score := Score + 1;
          KillBonus(9);
          SetTile(x, y, T_MONEY);
        end
        else if (Mk > 0) and (Random(3) = 0) then
        begin
          Msg('Lompakkosi kevenee!');
          Mk := Mk - Min(Mk, Random(4 * Level) + 1);
          SetTile(x, y, T_FLOOR);
        end;
      end;
    T_TEEKKARI:
      SetMsg('Lyöt opiskelijaa.');
    T_PORTSARI:
      begin
        SetMsg('Otat erää poken kanssa!');
        Killer := 'väkivalta';
        Voima := Voima - 1 - Random(2);
      end;
    T_NEEKERI:
      begin
        PoliceActive := True;
        if Friendly then SetMsg('Tapaat vanhan isäsi!')
        else SetMsg('Hakkaat neekeriä.');
        if Random(4) = 1 then
        begin
          Msg('Hän taintuu.');
          Voima := Voima + 1;
          SetTile(x, y, T_MONEY);
        end
        else begin Killer := 'neekeri'; Voima := Voima - 1; end;
      end;
    T_HINTTI:
      if Friendly then SetMsg('Tapaat mukavan pojan!')
      else
      begin
        PoliceActive := True;
        if Random(4) = 1 then
        begin
          SetMsg('Hakkaat homoa, joka luhistuu kadulle.');
          KillBonus(6);
          Voima := Voima + 1; Score := Score + 2;
          SetTile(x, y, T_POOP);
        end
        else SetMsg('Hakkaat homoa, joka nauttii siitä!');
      end;
    T_GORILLA:
      if Friendly then SetMsg('Sekoat elukkaan.')
      else
      begin
        SetMsg('Hakkaat gorillaa.');
        Dec(HP[t], d);
        if Equip[E_KETJUT] <> '' then Dec(HP[t]);
        if HP[t] < 1 then
        begin
          Msg('Otus uinahtaa.');
          Score := Score + 1;
          KillBonus(10);
          SetTile(x, y, T_FLOOR);
        end;
      end;
    T_SKIN:
      if Friendly then SetMsg('Taputat klania.')
      else
      begin
        SetMsg('Hakkaat kaljua.');
        Dec(HP[t], d);
        if HP[t] < 1 then
        begin
          Msg('Hän uinahtaa.');
          Score := Score + 2;
          if (Random(2) * Random(2) = 1) and (Kokemus < 18) then
          begin
            Inc(Kokemus);
            Voima := Voima + 3;
          end;
          SetTile(x, y, T_HINT);
        end;
      end;
    T_SNAKE:
      begin
        SetMsg('Isket luikertelijaa.');
        Dec(HP[t], d);
        if HP[t] < 1 then
        begin
          Msg('Otus uinahtaa.');
          Score := Score + 1;
          SetTile(x, y, T_MAKKARA);
        end;
      end;
    T_AIDS:
      begin
        SetMsg('Isket Sven-Olofia.');
        Dec(HP[t], d);
        if HP[t] < 1 then
        begin
          Msg('Hän uinahtaa.');
          Score := Score + 1;
          if Random(4) = 2 then SetTile(x, y, T_CHOCO) else SetTile(x, y, T_FLOOR);
        end;
      end;
    T_PAPPI:
      begin
        PoliceActive := True;
        SetMsg('Lyöt pappia mutta Jeesus suojelee häntä!');
      end;
    T_HIPPI:
      begin
        SetMsg('Tempaiset narkkista.');
        Dec(HP[t], d);
        if HP[t] <= 0 then
        begin
          Msg('Hän ottaa tripin!');
          Score := Score + 1;
          SetTile(x, y, T_FLOOR);
        end;
      end;
    T_TROKARI:
      SetMsg('Trokari väistää.');
    T_PIRKKO:
      if HP[t] > 0 then
      begin
        { milk one banked dot back out of the dot-eater }
        Dec(HP[t]);
        if RandomCell(25, 50, 3, 20, False, x, y) then SetTile(x, y, T_DOT);
        SetMsg('');
      end
      else
      begin
        SetMsg('Tuhoat pistepirkon.');
        Score := Score + 1;
        SetTile(x, y, T_FLOOR);
      end;
    T_DOG:
      if Friendly then SetMsg('Taputat hauvaa.')
      else
      begin
        SetMsg('Hakkaat koiraa.');
        Dec(HP[t], d);
        if Equip[E_KETJUT] <> '' then Dec(HP[t], 2);
        if HP[t] < 1 then
        begin
          Msg('Hauveli uinahtaa.');
          Score := Score + 1;
          KillBonus(9);
          SetTile(x, y, T_FLOOR);
        end;
      end;
    T_PUNK:
      begin
        SetMsg('Satutat rystysesi niitteihin.');
        Killer := 'punkkarit';
        Voima := Voima - 0.5;
      end;
    T_KINGI:
      begin
        SetMsg('Sivallat punkkarien kingiä.');
        Dec(HP[t], d);
        if HP[t] < 1 then PunkBlackout;
      end;
    T_JUPPI:
      begin
        SetMsg('Lyöt juppia.');
        Dec(HP[t], d);
        if HP[t] < 1 then
        begin
          Msg('Jupin kunto pettää.');
          Voima := Voima + 1; Score := Score + 2;
          KillBonus(11);
          SetTile(x, y, T_MONEY);
        end;
      end;
    T_KARKURI:
      begin
        SetMsg('Lyöt vankikarkuria.');
        Dec(HP[t], d);
        if HP[t] < 1 then
        begin
          Msg('Mies tuupertuu maahan.');
          Voima := Voima + 1; Score := Score + 2;
          KillBonus(12);
          SetTile(x, y, T_STILETTI);
        end;
      end;
    T_GHOST:
      begin
        SetMsg('Huidot aavetta!');
        Dec(HP[t], d);
        if HP[t] < 1 then
        begin
          Msg('Aave luhistuu kasaan!');
          Score := Score + 2;
          Voima := Voima + 5;
          if Random(10) = 9 then SetTile(x, y, T_MAKKARA) else SetTile(x, y, T_FLOOR);
        end;
      end;
    T_GRAVE:
      begin
        SetMsg('Eipäs tallata hautoja!');
        Killer := 'ilkeys';
        Voima := Voima - 0.5;
        { "Älä tallaa hautoja, aaveet suuttuvat" }
        if not FindTile(T_GHOST, n, n) and (Random(3) = 0) then
        begin
          Place(T_GHOST);
          HP[T_GHOST] := 8 + Random(8);
        end;
      end;
    T_FENCE:
      SetMsg('');
  end;
end;

{ ===========================================================================
  Player movement and pick-ups
  =========================================================================== }
procedure PickUp(t: TTile);
var n: Integer; a: Char;
begin
  case t of
    T_DOT:
      begin
        Inc(DotsGot);
        { 0x57BA: only said while you are blind ([0x2CD] = 0) }
        if Blind then SetMsg('Sokea kana löysi jyvän!');
      end;
    T_HAALARI:
      begin
        Equip[E_HAALARI] := 'Haalarit';
        SetMsg('Löydät haalarit.');
      end;
    T_PAMPPU:
      if Equip[E_PAMPPU] = '' then
      begin
        Equip[E_PAMPPU] := 'Pamppu';
        RecomputeWeapon;
        SetMsg('Takavarikoit kytän pampun.');
      end
      else SetMsg('Maassa lojuu risa pamppu.');
    T_STILETTI:
      if Equip[E_STILETTI] = '' then
      begin
        Equip[E_STILETTI] := 'Stiletti';
        RecomputeWeapon;
        SetMsg('Otat maassa lojuvan stiletin.');
      end
      else SetMsg('Maassa lojuu tylsä voiveitsi.');
    T_KETJUT:
      if Equip[E_KETJUT] = '' then
      begin
        Equip[E_KETJUT] := 'Ketjut';
        RecomputeWeapon;
        SetMsg('Korjaat ketjut talteen.');
      end
      else SetMsg('Näet maassa katkenneet ketjut.');
    T_CAMEL:
      begin
        if Equip[E_CAMEL] = '' then
        begin
          Score := Score + 1;
          Voima := Voima + 1;
        end;
        Equip[E_CAMEL] := 'Camel';
        RecomputeWeapon;
        SetMsg('Löydät aidot Camel bootsit!');
      end;
    T_KONJAKKI:
      begin
        SetMsg('Löydät konjakkipullon ja korkkaat sen!');
        Voima := Voima + 3;
        Prom := Prom + 0.5;
        Equip[E_HENNESSY] := 'Hennessy';
      end;
    T_MONEY:
      begin
        n := Random(21);
        Inc(Mk, n);
        SetMsg('Lompakkosi lihavoituu ' + IntToStr(n) + ' markalla.');
      end;
    T_HINT:
      HintPager;
    T_MILK:
      begin
        SetMsg('Kaadat maitokannun!');
        Score := Score - 1;
      end;
    T_CHOCO:
      begin
        SetMsg('Ahmit maahan pudonneen suklaalevyn!');
        Voima := Voima + 1;
        Score := Score + 1;
      end;
    T_APPLE:
      begin
        repeat
          a := AskKey('Tässä on omena, vaan uskallatko syödä? (k/e)');
        until a in ['k', 'K', 'e', 'E', #27];
        SetMsg('');
        if a in ['k', 'K'] then
          if Random(3) = 0 then
          begin
            SetMsg('Omena oli pilaantunut!');
            Score := Score - 1;
            Killer := 'myrkytys';
            Voima := Voima - 3;
          end
          else
          begin
            SetMsg('Omena oli herkullinen!');
            Voima := Voima + 1;
          end;
      end;
    T_SILAKKA:
      begin
        SetMsg('Hyi h..i, mikä täällä lemuaa!');
        Killer := 'myrkytys';
        Voima := Voima - 0.1;
      end;
    T_MAKKARA:
      begin
        Inc(InvKiekura);
        SetMsg('Kappas, kiekuralenkki.');
      end;
    T_POOP:
      begin
        SetMsg('Liukastut paskaan!');
        Score := Score - 1;
        Killer := 'kallonmurtuma';
        Voima := Voima - 1;
      end;
    T_WATER:
      if Equip[E_HAALARI] = '' then
      begin
        SetMsg('Et osaa uida, hukut!');
        Killer := 'hukkuminen';
        Die('Et osaa uida, hukut!');
      end
      else
      begin
        SetMsg('Varpaasi kastuvat!');
        Killer := 'vilustuminen';
        Voima := Voima - 0.3;
      end;
    T_KALJA:
      begin
        Inc(InvKalja);
        SetMsg('Miehen mittainen!');
      end;
    T_LONKKA:
      begin
        Inc(InvLonkka);
        SetMsg('1 Lonkka for the road!');
      end;
  else
    SetMsg('');
  end;
end;

function TryMove(dx, dy: Integer; var drunk: Boolean): Boolean;
{ Returns True if a turn was used. A step off the map edge is silently
  ignored (the dispatcher's edge pre-check), everything else costs a turn. }
var
  nx, ny: Integer;
  t: TTile;
begin
  drunk := False;
  if Prom > 3.0 then
  begin
    { drunk stagger (0x55A0): a random step instead of the intended one }
    drunk := True;
    dx := Random(3) - 1;
    dy := Random(3) - 1;
  end;
  nx := PX + dx; ny := PY + dy;
  if not InMap(nx, ny) then
  begin
    Result := drunk;      { a drunk stumble still wastes the turn }
    if drunk then SetMsg('');
    Exit;
  end;
  Result := True;
  if (dx = 0) and (dy = 0) then begin SetMsg(''); Exit; end;
  t := Map[nx, ny];
  if t = T_WALL then begin BumpWall; Exit; end;
  if t > $1D then begin Attack(nx, ny); Exit; end;
  { walkable: move, then handle what was there }
  PX := nx; PY := ny;
  if not (t in [T_EXIT, T_WATER]) then Map[nx, ny] := T_FLOOR;
  PickUp(t);
end;

{ ===========================================================================
  Throw (t) -- v1.17 spec (file 0x81A7..0x9290)
  =========================================================================== }
function ThrowDamage(key: Char): Integer;
begin
  case key of
    '1': Result := 1;  '2': Result := 2;  '3': Result := 3;  '4': Result := 4;
    '5': Result := 2;  '8': Result := 10; '9': Result := 3;
    'a': Result := 1;  'b': Result := 1;
  else Result := 0;
  end;
end;

function DoThrow: Boolean;
var
  key, dkey: Char;
  dx, dy, entry, dmg, cx, cy, nx, ny: Integer;
  t, item: TTile;
begin
  Result := False;
  DrawInventory;
  repeat
    key := AskKey('Mitä viskaat (1-9 tavara, a=kalja b=lonkka c=kiekura, 0=peru) >');
  until (key in ['0'..'9', 'a'..'c', #27]);
  SFill(1, 2, 12, 15);
  if not (key in ['1'..'9', 'a'..'c']) then begin SetMsg(''); Exit; end;
  item := T_FLOOR;
  if key in ['1'..'9'] then
  begin
    entry := Ord(key) - Ord('0') + 2;
    if Equip[entry] = '' then begin SetMsg('Sinulla ei ole sellaista.'); Exit; end;
    item := entry;               { the item's tile code equals its entry }
    if entry > E_CAMEL then item := T_FLOOR;
  end
  else
    case key of
      'a': if InvKalja = 0 then begin SetMsg('Sinulla ei ole kaljaa.'); Exit; end
           else item := T_KALJA;
      'b': if InvLonkka = 0 then begin SetMsg('Sinulla ei ole lonkkaa.'); Exit; end
           else item := T_LONKKA;
      'c': if InvKiekura = 0 then begin SetMsg('Sinulla ei ole kiekuraa.'); Exit; end
           else item := T_MAKKARA;
    end;

  repeat
    dkey := AskKey('Suunta >');
  until dkey in ['.', 'h', 'j', 'k', 'l', 'y', 'u', 'b', 'n', #27];
  dx := 0; dy := 0;
  case dkey of
    'h': dx := -1;
    'l': dx := 1;
    'k': dy := -1;
    'j': dy := 1;
    'y': begin dx := -1; dy := -1; end;
    'u': begin dx := 1; dy := -1; end;
    'b': begin dx := -1; dy := 1; end;
    'n': begin dx := 1; dy := 1; end;
  end;
  if ((dx = 0) and (dy = 0)) or not InMap(PX + dx, PY + dy) then
  begin
    SetMsg('Hassu suunta!');
    Exit;
  end;

  { the item leaves the inventory }
  dmg := ThrowDamage(key);
  if key in ['1'..'9'] then
  begin
    Equip[Ord(key) - Ord('0') + 2] := '';
    RecomputeWeapon;
  end
  else
    case key of
      'a': Dec(InvKalja);
      'b': Dec(InvLonkka);
      'c': Dec(InvKiekura);
    end;

  { flight: '*' trail over empty cells, stop at anything bigger than $1D }
  cx := PX; cy := PY;
  repeat
    nx := cx + dx; ny := cy + dy;
    if not InMap(nx, ny) then Break;
    cx := nx; cy := ny;
    t := Map[cx, cy];
    if t > $1D then Break;
    if t = T_FLOOR then SPut(cx, cy, '*', A_YELLOW);
  until False;
  CurCol := PX; CurRow := PY;
  SFlush;
  Sleep(120);

  t := Tile(cx, cy);
  SetMsg('');
  if not InMap(cx, cy) or ((t <= $1D) and (t <> T_FLOOR)) then
    SetMsg('Heittosi suhahtaa ilmaan!')
  else
    case t of
      T_FLOOR:
        if item <> T_FLOOR then SetTile(cx, cy, item);
      T_MUMMO:
        begin
          SetTile(cx, cy, T_MONEY);
          SetMsg('Kolkkaat mummelin brutaalisti.');
          PoliceActive := True;
        end;
      T_KAKE:     SetMsg('Kiusaat Kakea.');
      T_VARAS:
        begin
          Dec(HP[t], dmg);
          if HP[t] <= 0 then
          begin
            SetMsg('Hän tuupertuu ketoon.');
            SetTile(cx, cy, T_MONEY);
          end
          else SetMsg('Osut varkaaseen.');
        end;
      T_TEEKKARI:
        if key in ['a', 'b'] then
        begin
          SetTile(cx, cy, T_FLOOR);
          SetMsg('Teekkari nappaa pullon lennosta ja katoaa.');
        end
        else SetMsg('Ohi meni!');
      T_PORTSARI:
        begin
          Dec(HP[t], dmg);
          if HP[t] <= 0 then
          begin
            SetMsg('Hän sammahtaa.');
            SetTile(cx, cy, T_FLOOR);
            PortsariOn := False;
          end
          else SetMsg('Osut portsariin.');
        end;
      T_PAPPI:
        begin
          Dec(HP[t], dmg);
          SetMsg('Osut pappismieheen.');
          PoliceActive := True;
        end;
      T_HIPPI:
        begin
          SetMsg('Hän sammahtaa ja kolehtihaavi aukeaa.');
          SetTile(cx, cy, T_MONEY);
          if key = '3' then SetTile(cx, cy, T_STILETTI);
        end;
      T_TROKARI:
        if key = '3' then
        begin
          { "Lentävä veitsi pysäyttää trokarin" }
          SetMsg('Lentävä veitsi pysäyttää trokarin.');
          SetTile(cx, cy, T_KALJA);
        end
        else SetMsg('Trokari väistää.');
      T_PUNK:     SetMsg('Heittosi osuu punkkariin.');
      T_KINGI:
        begin
          SetMsg('Heittosi osuu kingiin.');
          Dec(HP[t], dmg);
          if HP[t] < 1 then PunkBlackout;
        end;
      T_NEEKERI, T_PIRKKO, T_JUPPI:
        SetMsg('Ohi meni!');
      T_KARKURI:  SetMsg('Et osunut');
      T_GORILLA:
        begin
          SetMsg('Viskaat gorillaa.');
          Dec(HP[t], dmg);
          if HP[t] < 1 then begin Msg('Otus uinahtaa.'); SetTile(cx, cy, T_FLOOR); end;
        end;
      T_SKIN:
        begin
          SetMsg('Viskaat klania.');
          Dec(HP[t], dmg);
          if HP[t] < 1 then begin Msg('Hän uinahtaa.'); SetTile(cx, cy, T_HINT); end;
        end;
      T_SNAKE:
        begin
          SetMsg('Viskaat käärmettä.');
          Dec(HP[t], dmg);
          if HP[t] < 1 then begin Msg('Otus uinahtaa.'); SetTile(cx, cy, T_MAKKARA); end;
        end;
      T_AIDS:     SetMsg('Osut potilaaseen.');
      T_HINTTI:   SetMsg('Hintti väistää.');
      T_DOG:
        begin
          SetMsg('Viskaat rakkia.');
          Dec(HP[t], dmg);
          if HP[t] < 1 then begin Msg('Hauveli uinahtaa.'); SetTile(cx, cy, T_FLOOR); end;
        end;
      T_POLICE1..T_POLICE5:
        begin
          SetMsg('Heität skoudea!');
          PoliceActive := True;
          Dec(HP[t], 1 + dmg);
          if HP[t] <= 0 then
          begin
            SetMsg('Poliisi pökertyy!');
            if Equip[E_PAMPPU] = '' then SetTile(cx, cy, T_PAMPPU)
            else if Random(10) = 9 then SetTile(cx, cy, T_MONEY)
            else SetTile(cx, cy, T_FLOOR);
          end;
        end;
    else
      SetMsg('Heittosi suhahtaa ilmaan!');
    end;
  Result := True;
end;

{ ===========================================================================
  Creatures' turn (NPC update chain 0x9EC4..0xE0E0)
  =========================================================================== }
procedure RandomFloorCell(var rx, ry: Integer);
begin
  if not RandomCell(20, 60, 3, 20, False, rx, ry) then
  begin
    rx := -1; ry := -1;
  end;
end;

procedure UpdatePolice;
{ police block (0xA7EC..0xB09F): asleep until a crime, then every officer
  walks one cell toward the player and beats him when adjacent }
var
  n, p, x, y, dx, dy, tx, ty, fx, fy, rx, ry: Integer;
  cx, cy: array[1..32] of Integer;
  cv: array[1..32] of TTile;
  t: TTile;

  function Arrest(const s: string): Boolean;
  begin
    Msg(s);
    Result := True;
  end;

begin
  if not PoliceActive then Exit;
  n := 0;
  for y := MAP_Y0 to MAP_Y1 do
    for x := MAP_X0 to MAP_X1 do
      if (Map[x, y] in [T_POLICE1..T_POLICE5]) and (n < 32) then
      begin
        Inc(n); cx[n] := x; cy[n] := y; cv[n] := Map[x, y];
      end;
  for p := 1 to n do
  begin
    if Map[cx[p], cy[p]] <> cv[p] then Continue;
    dx := PX - cx[p]; dy := PY - cy[p];
    if (Abs(dx) < 2) and (Abs(dy) < 2) then
    begin
      if Friendly then Msg('Ystävällinen herra ojentaa sinulle kukkasen!')
      else
      begin
        Msg('Poliisi pamputtaa!');
        Killer := 'poliisi';
        Voima := Voima - (1 + Random(3));
      end;
      Continue;
    end;
    tx := cx[p] + Sgn(dx); ty := cy[p] + Sgn(dy);
    if not InMap(tx, ty) then begin tx := cx[p]; ty := cy[p]; end;
    fx := tx; fy := ty;
    t := Map[tx, ty];
    case t of
      T_FLOOR..T_DOT - 1:
        ;                                        { walks over it }
      T_DOT:
        begin
          RandomFloorCell(rx, ry);               { the dot is moved away }
          if rx > 0 then Map[rx, ry] := T_DOT
          else begin fx := cx[p]; fy := cy[p]; end;
        end;
      T_EXIT:
        begin
          RandomFloorCell(rx, ry);
          if rx > 0 then begin fx := rx; fy := ry; end
          else begin fx := cx[p]; fy := cy[p]; end;
        end;
      T_MUMMO:    Arrest('Poliisit vievät eukon turvaan.');
      T_KAKE:     Arrest('Poliisit nappaavat kaverisi Kaken.');
      T_VARAS:    Arrest('Poliisit pidättävät taskuvarkaan.');
      T_PORTSARI: begin Arrest('Koppalakit nappaavat huijari-poken.'); PortsariOn := False; end;
      T_HINTTI:   Arrest('Poliisi poistaa hintin.');
      T_GORILLA:  Arrest('Virkavalta poistaa karkulaisen.');
      T_TROKARI:  Arrest('Koppalakit nappaavat trokarin.');
      T_HIPPI:    Arrest('Poliisi vie hipin.');
      T_DOG:      Arrest('Virkavalta poistaa vesikauhuisen hurtan.');
      T_KARKURI:  Arrest('Poliisit pidättävät vankikarkurin.');
      T_PIRKKO:
        begin
          TeleportPirkko(tx, ty);
          if Map[tx, ty] <> T_FLOOR then begin fx := cx[p]; fy := cy[p]; end;
        end;
      T_TEEKKARI:
        begin
          { the officer swaps places with the student }
          Map[cx[p], cy[p]] := T_TEEKKARI;
          Map[tx, ty] := cv[p];
          Continue;
        end;
    else
      { wall, water, another officer, ...: step aside or stay }
      if tx = PX then
      begin
        if IsFree(tx + 1, ty) then fx := tx + 1
        else if IsFree(tx - 1, ty) then fx := tx - 1
        else begin fx := cx[p]; fy := cy[p]; end;
      end
      else
      begin
        if IsFree(tx, ty + 1) then fy := ty + 1
        else if IsFree(tx, ty - 1) then fy := ty - 1
        else begin fx := cx[p]; fy := cy[p]; end;
      end;
      if (fx <> cx[p]) or (fy <> cy[p]) then
        if not IsFree(fx, fy) then begin fx := cx[p]; fy := cy[p]; end;
    end;
    if (fx = PX) and (fy = PY) then begin fx := cx[p]; fy := cy[p]; end;
    if (fx <> cx[p]) or (fy <> cy[p]) then
    begin
      Map[cx[p], cy[p]] := T_FLOOR;
      Map[fx, fy] := cv[p];
    end;
  end;
end;

procedure UpdateDog;
{ dog block (0xCA4B..0xCEA1): bites when adjacent, otherwise walks toward
  the player and infects whatever it walks into }
var
  x, y, tx, ty: Integer;
  t: TTile;
  moved: Boolean;

  procedure Infect(const s: string);
  begin
    Msg(s);
    Map[tx, ty] := T_FLOOR;
  end;

begin
  if not FindTile(T_DOG, x, y) then Exit;
  if Adjacent(x, y) then
  begin
    if Friendly then Exit;
    Msg('Piski puraisee sinua!');
    Killer := 'rakkikoira';
    Voima := Voima - (1 + Random(4));
    Exit;
  end;
  tx := x + Sgn(PX - x); ty := y + Sgn(PY - y);
  t := Tile(tx, ty);
  case t of
    T_MUMMO:   Infect('Mummeli jää hurtan hampaisiin.');
    T_KAKE:    Infect('Kake saa verenmyrkytyksen.');
    T_NEEKERI: Infect('Nekru saa verenmyrkytyksen.');
    T_HINTTI:  Infect('Hintti saa verenmyrkytyksen.');
    T_TROKARI: Infect('Trokari saa verenmyrkytyksen.');
    T_PAPPI:   Infect('Pappi saa verenmyrkytyksen.');
    T_HIPPI:   Infect('Hippi saa verenmyrkytyksen.');
    T_POLICE1..T_POLICE5: Infect('Poliisi saa verenmyrkytyksen.');
    T_PIRKKO:  TeleportPirkko(tx, ty);
    T_GHOST:
      begin
        Msg('Haamut tappavat rakin.');
        Map[x, y] := T_FLOOR;
        Exit;
      end;
    T_EXIT:
      begin
        Map[x, y] := T_FLOOR;                  { the dog leaves the level }
        Exit;
      end;
  end;
  moved := False;
  if IsFree(tx, ty) then moved := True
  else
  begin
    tx := x + Sgn(PX - x); ty := y;
    if (tx <> x) and IsFree(tx, ty) then moved := True
    else
    begin
      tx := x; ty := y + Sgn(PY - y);
      if (ty <> y) and IsFree(tx, ty) then moved := True;
    end;
  end;
  if moved then
  begin
    Map[x, y] := T_FLOOR;
    Map[tx, ty] := T_DOG;
  end;
end;

procedure UpdatePirkko;
{ dot-eater block (0xC943..0xCA4B): teleports to a random empty-or-dot
  cell every turn, eating (banking) the dot it lands on }
var x, y, rx, ry: Integer;
begin
  if not FindTile(T_PIRKKO, x, y) then Exit;
  if not RandomCell(22, 58, 3, 20, True, rx, ry) then Exit;
  if Map[rx, ry] = T_DOT then Inc(HP[T_PIRKKO]);
  Map[x, y] := T_FLOOR;
  Map[rx, ry] := T_PIRKKO;
end;

procedure HippieTrip;
{ "Hippi huitoo sinua piikillään, ja osuu!" -- a long hallucination }
begin
  if (Length(MsgLine) > 0) and (MsgLine[Length(MsgLine)] = '!') then
    MsgLine := Copy(MsgLine, 1, Length(MsgLine) - 1) + ', ja osuu!'
  else Msg('Hippi osuu!');
  Voima := Voima + 2;
  Prom := Prom + 1.2;
  Score := Score + 1;
  Friendly := True;
  FriendlyTurns := 40 + Random(40);
end;

procedure UpdateNeighbours;
{ what every static creature does to a player standing next to it }
var
  x, y: Integer;
  t: TTile;
  done: array[0..T_MAXCODE] of Boolean;
  a: Char;
  n: Integer;
begin
  FillChar(done, SizeOf(done), 0);
  for y := PY - 1 to PY + 1 do
    for x := PX - 1 to PX + 1 do
    begin
      if Dead or not InMap(x, y) then Continue;
      t := Map[x, y];
      if (t > T_MAXCODE) or done[t] then Continue;
      done[t] := True;
      case t of
        T_GHOST:
          if Friendly then Msg('Sinua kutittaa!')
          else
          begin
            Msg('Aave hakkaa sinua!');
            Killer := 'haamu';
            Voima := Voima - (1 + Random(2));
          end;
        T_VARAS:
          if Mk > 0 then
          begin
            Killer := 'taskuvaras';
            Voima := Voima - 0.2;
            if Random(4) = 0 then
            begin
              Msg('Käteiskassasi kutistuu.');
              Mk := Mk - Min(Mk, Random(4 * Level) + 1);
              Map[x, y] := T_FLOOR;
            end;
          end;
        T_KARKURI:
          begin
            Msg('Vankikarkuri pahoinpitelee sinua.');
            Killer := 'murha';
            Voima := Voima - Random(5);
          end;
        T_TEEKKARI:
          if (Mk > 30) and (Random(2) = 0) then
          begin
            if Equip[E_APY] <> '' then
              Msg('Teekkari ylipuhuu sinut ostamaan äpyn, vaikka sinulla oli jo!')
            else
            begin
              Msg('Teekkari ylipuhuu sinut ostamaan äpyn.');
              Inc(Kokemus);
            end;
            Dec(Mk, 30);
            Equip[E_APY] := 'Äpy';
            Voima := Voima + 1;
            Score := Score + 1;
            Map[x, y] := T_FLOOR;
          end;
        T_KAKE:
          if InvKalja > 0 then
          begin
            if MsgLine <> '' then begin DrawMsg; PressAny; end;
            if AskYesNo('Kamusi Kake pummaa kaljaa, annatko (k/e)?') then
            begin
              SetMsg('Kake kiittää ja katoaa.');
              Voima := Voima + 1;
              Dec(InvKalja);
              Map[x, y] := T_FLOOR;
            end
            else
            begin
              SetMsg('Kake taputtaa palleaasi.');
              Killer := 'Kake';
              Voima := Voima - 1;
            end;
          end;
        T_PAPPI:
          begin
            Msg('Pappi saarnaa sinulle viinan vaaroista.');
            if ((Random * 3 < Prom) or Friendly) and (InvKalja + InvLonkka > 0) then
            begin
              Msg('Hetken hairahduksesta kaadat juomasi maahan!');
              Voima := Voima + 1;
              Inc(Kokemus);
              Score := Score + 1;
              InvKalja := 0; InvLonkka := 0;
            end;
          end;
        T_HINTTI:
          begin
            if Friendly then Msg('Joku hyväilee sinua.')
            else Msg('Hinttari hiplaa sinua!');
            Killer := 'hinttailu';
            Voima := Voima - 0.5;
          end;
        T_TROKARI:
          if Mk > 50 then
          begin
            if MsgLine <> '' then begin DrawMsg; PressAny; end;
            repeat
              a := AskKey('Hämärä veikko tarjoaa taikajuomaa pullostaan, 50 mk (k/e)?');
            until a in ['k', 'K', 'e', 'E', #27];
            if a in ['k', 'K'] then
            begin
              Dec(Mk, 50);
              if Random(2) = 1 then
              begin
                SetMsg('Kurkkuasi lämmittää mukavasti.');
                Voima := Voima + 2;
                Prom := Prom + 1.2;
                Score := Score + 1;
                if Kokemus < 10 then Inc(Kokemus);
              end
              else
              begin
                SetMsg('Tulet sokeaksi.');
                Killer := 'masinol';
                Voima := Voima - 2;
                Blind := True;
              end;
            end
            else
            begin
              SetMsg('Tunnet vihlaisun maksassasi!');
              Killer := 'masinol';
              Voima := Voima - 1;
            end;
            Map[x, y] := T_FLOOR;
          end;
        T_JUPPI:
          begin
            Msg('Juppi uhoaa.');
            Killer := 'juppi';
            Voima := Voima - 0.2;
          end;
        T_HIPPI:
          begin
            Msg('Hippi huitoo sinua piikillään!');
            Killer := 'piikki';
            Voima := Voima - 0.5;
            if (not Friendly) and (Voima + Random(13) > 21) then HippieTrip;
          end;
        T_SKIN:
          if not Friendly then
          begin
            Msg('Skinhead murjoo sinua apinan raivolla!');
            n := Min(Trunc(Voima / 3), 4);
            if n < 0 then n := 0;
            Killer := 'väkivalta';
            Voima := Voima - (1 + Random(n + 1));
          end;
        T_SNAKE:
          begin
            Msg('Käärme puree sinua.');
            Killer := 'käärme';
            Voima := Voima - 1;
            if (Random(Max(1, Trunc(Voima))) = 0) and (Equip[E_STILETTI] = '') then
            begin
              { 0xD4B8: Voima := -1, so the end-of-turn check collapses you }
              DeathPre := 'Valitettavasti purema oli myrkyllinen ...';
              Killer := 'myrkytys';
              SetMsg(DeathPre);
              Voima := -1;
            end;
          end;
        T_GORILLA:
          if not Friendly then
          begin
            Msg('Gorilla hakkaa sinua.');
            Killer := 'gorilla';
            Voima := Voima - (1 + Random(2));
          end;
        T_AIDS:
          begin
            Msg('Sven-Olof halaa sinua.');
            if (not Aids) and (Random(4) = 0) then
            begin
              Msg('Samalla HIV-virus siirtyy suoneesi!');
              Aids := True;
            end;
          end;
      end;
    end;
end;

procedure UpdatePunks;
{ 0xA1AC: while you stand in the gang's mouth column it beats you up;
  otherwise the whole gang steps one column left and lines up with your row }
begin
  if not PunksOn or Dead then Exit;
  if PX = KingX - 1 then
  begin
    if Friendly then
    begin
      Msg('Tanssit jonkun mukavan pojan kanssa.');
      Killer := 'punkkarit';
      Voima := Voima - 1;
    end
    else
    begin
      Msg('Punkkarit muiluttavat sinua!');
      Killer := 'punkkarit';
      Voima := Voima - 2;
    end;
  end
  else if KingX - 2 >= MAP_X0 then   { the original walked off the map }
  begin
    PunkErase;
    KingY := PY;
    if KingY < MAP_Y0 then KingY := MAP_Y0;
    if KingY > MAP_Y1 then KingY := MAP_Y1;
    Dec(KingX);
    PunkStamp;
  end;
end;

procedure UpdateCreatures;
begin
  UpdatePunks;
  UpdatePirkko;
  UpdateDog;
  UpdatePolice;
  UpdateNeighbours;
end;

{ ===========================================================================
  End of turn (per-turn handler 0xE0E8..0xE36A)
  =========================================================================== }
procedure EndTurn;
var vx: Integer;
begin
  Turn := Turn + 1;
  if Friendly then
  begin
    Dec(FriendlyTurns);
    if FriendlyTurns <= 0 then Friendly := False;
  end;

  { drunk state text + effect }
  if Aids then
  begin
    StateText := ST_AIDS;
    Killer := 'AIDS';
    Voima := Voima - 0.2;
  end
  else if Prom < 0.05 then StateText := ST_SICK
  else if Prom > 4.0 then
  begin
    StateText := ST_VOMIT;
    Killer := 'alkoholimyrkytys';
    Voima := Voima - 1;
    vx := PX + 1;
    if IsFree(vx, PY) then Map[vx, PY] := T_POOP;
  end
  else if Prom > 3.0 then StateText := ST_STAGGER
  else StateText := ST_BLANK;

  { Promille decay and Kankkunen }
  if Prom >= 0 then
    Prom := Prom - 0.00054
  else
  begin
    Killer := 'krapula';
    Die(KANKKUNEN);
  end;

  { Voima regeneration (0x9B6D): Random/87 + Level/300 per turn }
  Voima := Voima + Random / 87 + Level / 300;

  CheckDeath;
end;

{ ===========================================================================
  Descent ('>')
  =========================================================================== }
procedure WinGame;
begin
  Score := Score + 20 + Voima + Trunc(10 * Prom);
  Won := True;
end;

function TryDescend: Boolean;
{ Returns True if a turn passed. }
var toll: Integer;
begin
  Result := False;
  if (PX <> EXIT_X) or (PY <> EXIT_Y) then
  begin
    SetMsg('Ei ei, vain portaita pitkin');
    Exit;
  end;
  if DotsGot < DotsNeeded then
  begin
    SetMsg('Ensin pisteet ...');
    Exit;
  end;
  if PortsariOn and (Tile(PORTSARI_X, PORTSARI_Y) = T_PORTSARI) then
  begin
    toll := Min(5 * Level, Mk);
    if not AskYesNo('Poke vaatii ' + IntToStr(toll) + ' mk juomarahaa, maksatko (k/e) >') then
    begin
      SetMsg('Portsari ei päästä sinua portista.');
      Result := True;
      Exit;
    end;
    Dec(Mk, toll);
  end;
  Result := True;
  if Equip[E_HENNESSY] <> '' then
  begin
    WinGame;
    Exit;
  end;
  NextLevel;
  SetMsg('');
end;

{ ===========================================================================
  Debug: dump level 1 (screen coords x=14..79, y=3..22, one byte per cell)
  =========================================================================== }
procedure DumpMapToFile(const fn: string);
var
  fs: TFileStream;
  x, y: Integer;
  b: Byte;
begin
  Level := 1;
  BuildLevel;
  fs := TFileStream.Create(fn, fmCreate);
  try
    for y := MAP_Y0 to MAP_Y1 do
      for x := MAP_X0 to MAP_X1 do
      begin
        b := Byte(Map[x, y]);
        fs.WriteBuffer(b, 1);
      end;
  finally
    fs.Free;
  end;
end;

{ ===========================================================================
  Main
  =========================================================================== }
procedure InitDataDirs;
var exeDir: string;
begin
  DataDirs := TStringList.Create;
  DataDirs.Add(IncludeTrailingPathDelimiter(GetCurrentDir));
  exeDir := IncludeTrailingPathDelimiter(ExtractFilePath(ExpandFileName(ParamStr(0))));
  if DataDirs.IndexOf(exeDir) < 0 then DataDirs.Add(exeDir);
  DataDirs.Add(IncludeTrailingPathDelimiter(GetCurrentDir) + 'spurg117' + PathDelim);
  DataDirs.Add(exeDir + 'spurg117' + PathDelim);
end;

var
  StartLevel: Integer = 1;
  StartMk: Integer = 0;

procedure NewGame;
var i: Integer;
begin
  Level := StartLevel; Mk := StartMk; Score := StartLevel;
  Voima := 10; Prom := 0.2; Turn := 0;
  InvKalja := 1; InvLonkka := 0; InvKiekura := 1;   { start kit (0x4FAB) }
  for i := 0 to 11 do Equip[i] := '';
  Equip[E_HAALARI] := 'Haalarit';
  Kokemus := 1;
  Friendly := False; FriendlyTurns := 0; Aids := False;
  RecomputeWeapon;
  BuildLevel;
end;

function ReadName: string;
var ch: Char; s: string;
begin
  SClear;
  s := '';
  repeat
    SFill(1, 1, SCRW, 1);
    SPut(1, 1, 'Kerropa nimesi >' + s);
    CurRow := 1; CurCol := 17 + ULen(s);
    CursorOn := True;
    ch := ReadKey;
    CursorOn := False;
    if ch in [#13, #10] then Break;
    if ch = #4 then Break;
    if ch in [#8, #127] then
    begin
      while (Length(s) > 0) and ((Ord(s[Length(s)]) and $C0) = $80) do
        Delete(s, Length(s), 1);
      if Length(s) > 0 then Delete(s, Length(s), 1);
    end
    else if (ch >= ' ') and (ULen(s) < 9) then
      s := s + ch
    else if (Ord(ch) >= $80) and (Length(s) < 20) then
      s := s + ch;     { UTF-8 continuation bytes of ä/ö }
  until False;
  Result := Trim(s);
  if Result = '' then Result := 'Spurgu';
end;

procedure MainGame;
var
  ch, repeatCh: Char;
  acted, wasRepeat, drunk, doRedraw: Boolean;
  i, seed: Integer;
  rec: TRec;

  procedure Move(c: Char; dx, dy: Integer; upper: Boolean);
  begin
    acted := TryMove(dx, dy, drunk);
    if (upper or wasRepeat) and acted and not drunk and (MsgLine = '') and
       (Map[PX + dx, PY + dy] = T_FLOOR) and InMap(PX + dx, PY + dy) then
      repeatCh := c
    else
      repeatCh := #0;
  end;

begin
  seed := -1;
  i := 1;
  while i <= ParamCount do
  begin
    if ParamStr(i) = '--dumpmap' then
    begin
      DumpMapToFile(ParamStr(i + 1));
      Exit;
    end
    else if ParamStr(i) = '--mono' then UseColor := False
    else if (ParamStr(i) = '--seed') and (i < ParamCount) then
    begin
      seed := StrToIntDef(ParamStr(i + 1), 0);
      Inc(i);
    end
    else if (ParamStr(i) = '--level') and (i < ParamCount) then
    begin
      StartLevel := StrToIntDef(ParamStr(i + 1), 1);
      if (StartLevel < 1) or (StartLevel > MAX_LEVEL) then StartLevel := 1;
      Inc(i);
    end
    else if (ParamStr(i) = '--mk') and (i < ParamCount) then
    begin
      StartMk := StrToIntDef(ParamStr(i + 1), 0);
      Inc(i);
    end;
    Inc(i);
  end;
  if GetEnvironmentVariable('NO_COLOR') <> '' then UseColor := False;
  if seed >= 0 then RandSeed := seed else Randomize;

  SetRaw;
  fpSignal(SIGTERM, @SignalHandler);
  fpSignal(SIGHUP, @SignalHandler);
  OutStr(#27'[?1049h'#27'[2J'#27'[H');
  ScreenInited := True;
  ForceRedraw := True;
  SClear;

  Dead := False; Won := False; Saved := False; DeathPre := '';
  Killer := 'tuntematon';
  if FileExists(SavePath) and
     AskYesNo('Löytyi talletettu peli. Jatketaanko sitä (k/e)?') and LoadGame then
  begin
    DeleteFile(SavePath);
    SetMsg('Tervetuloa takaisin, ' + PlayerName + '!');
  end
  else
  begin
    PlayerName := ReadName;
    NewGame;
    SetMsg('Heippa ' + PlayerName + ', tervetuloa SpurguXiin!');
  end;
  StateText := ST_BLANK;
  SClear;
  FullRedraw;

  repeatCh := #0;
  while not Dead and not Won and not Saved do
  begin
    wasRepeat := repeatCh <> #0;
    if wasRepeat then
    begin
      ch := repeatCh;
      SFlush;
      Sleep(25);
    end
    else ch := ReadKey;
    acted := False;
    doRedraw := True;
    if not wasRepeat then SetMsg('');
    MsgLine := '';

    case ch of
      'h': Move('h', -1, 0, False);   'H': Move('h', -1, 0, True);
      'j': Move('j', 0, 1, False);    'J': Move('j', 0, 1, True);
      'k': Move('k', 0, -1, False);   'K': Move('k', 0, -1, True);
      'l': Move('l', 1, 0, False);    'L': Move('l', 1, 0, True);
      'y': Move('y', -1, -1, False);  'Y': Move('y', -1, -1, True);
      'u': Move('u', 1, -1, False);   'U': Move('u', 1, -1, True);
      'b': Move('b', -1, 1, False);   'B': Move('b', -1, 1, True);
      'n': Move('n', 1, 1, False);    'N': Move('n', 1, 1, True);
      '.': begin acted := True; repeatCh := #0; end;
      'q': begin DoDrink; acted := True; repeatCh := #0; end;
      'e': begin DoEat; acted := True; repeatCh := #0; end;
      'i': begin repeatCh := #0; DoInventory; end;
      't': begin repeatCh := #0; acted := DoThrow; end;
      '>': begin repeatCh := #0; acted := TryDescend; end;
      'v': begin
             repeatCh := #0;
             SetMsg('SpurguX Vs. 1.17, Last edited 09.09.1987, (C) Petri Niska');
           end;
      '?': begin repeatCh := #0; ShowHelp; end;
      '/': begin repeatCh := #0; Sepustus; end;
      'p': begin repeatCh := #0; MsgLine := LastMsg; end;        { v2.24 }
      'c': begin repeatCh := #0; SetMsg('Kello on ' + FormatDateTime('hh:nn', Now) + '.'); end;
      #12: begin repeatCh := #0; ForceRedraw := True; end;       { ^L }
      'S': begin                                                 { v2.24 }
             repeatCh := #0;
             if AskYesNo('Talletetaanko peli ja lopetetaan (k/e)?') then
             begin
               try
                 SaveGame;
                 Saved := True;
               except
                 SetMsg('Talletus epäonnistui!');
               end;
             end
             else SetMsg('');
           end;
      'Q', #3, #4:
           begin
             repeatCh := #0;
             if AskYesNo('Todella luovutat (k/e) ? ') then
             begin
               Dead := True;
               DeathCause := '';
               Killer := 'itsemurha';
             end
             else SetMsg('');
           end;
      #27, #0: repeatCh := #0;
    else
      repeatCh := #0;
      if ch >= ' ' then SetMsg('Tuntematon komento ' + ch);
    end;

    if acted and not Dead and not Won then
    begin
      UpdateCreatures;
      if not Dead then EndTurn;
      if repeatCh <> #0 then
        if (MsgLine <> '') or Dead then repeatCh := #0;
    end;
    if MsgLine <> '' then LastMsg := MsgLine;
    if doRedraw then FullRedraw;
  end;

  if Saved then
  begin
    RestoreTerm;
    WriteLn('Peli talletettu (', SavePath, '). Näkemiin, ', PlayerName, '!');
    Exit;
  end;

  if Won then
  begin
    SClear;
    SPut(1, 1, 'ONNITTELUT, ' + PlayerName, A_YELLOW);
    SPut(1, 2, ' TEIT SEN ! Sinussa on ainesta !', A_YELLOW);
    CurRow := 3; CurCol := 1;
    ReadKeyAfterPause(1500);
  end
  else if DeathCause <> '' then
  begin
    { each death has its own message (v1.17 and v2.24 alike); Kankkunen is
      only the sobering-up death, not an epilogue to every death }
    if DeathPre <> '' then
    begin
      SetMsg(DeathPre);
      FullRedraw;
      PressAnySlow;
    end;
    SetMsg(DeathCause);
    FullRedraw;
    PressAnySlow;
  end;
  rec := MakeRec;
  ShowHiscore(rec);
  RestoreTerm;
  if Won then
    WriteLn('ONNITTELUT, ', PlayerName, ' ! TEIT SEN ! Sinussa on ainesta !');
end;

begin
  InitDataDirs;
  try
    MainGame;
  finally
    RestoreTerm;
  end;
end.
