#include "selfdrive/ui/replay/consoleui.h"

#include <QApplication>
#include <initializer_list>

#include "selfdrive/common/version.h"

namespace {

const int BORDER_SIZE = 3;

const std::initializer_list<std::pair<std::string, std::string>> keyboard_shortcuts[] = {
  {
    {"s", "+10s"},
    {"shift+s", "-10s"},
    {"m", "+60s"},
    {"shift+m", "-60s"},
    {"space", "Pause/Resume"},
    {"e", "Next Engagement"},
    {"d", "Next Disengagement"},
  },
  {
    {"enter", "Enter seek request"},
    {"x", "+/-Replay speed"},
    {"q", "Exit"},
  },
};

enum Color {
  Default,
  Debug,
  Yellow,
  Green,
  Red,
  BrightWhite,
  Engaged,
  Disengaged,
};

void add_str(WINDOW *w, const char *str, Color color = Color::Default, bool bold = false) {
  if (color != Color::Default) wattron(w, COLOR_PAIR(color));
  if (bold) wattron(w, A_BOLD);
  waddstr(w, str);
  if (bold) wattroff(w, A_BOLD);
  if (color != Color::Default) wattroff(w, COLOR_PAIR(color));
}

std::string format_seconds(int s) {
  int total_minutes = s / 60;
  int seconds = s % 60;
  int hours = total_minutes / 60;
  int minutes = total_minutes % 60;
  return util::string_format("%02d:%02d:%02d", hours, minutes, seconds);
}

}  // namespace

ConsoleUI::ConsoleUI(Replay *replay, QObject *parent) : replay(replay), sm({"carState", "liveParameters"}), QObject(parent) {
  // Initialize curses
  initscr();
  clear();
  curs_set(false);
  cbreak();  // Line buffering disabled. pass on everything
  noecho();
  keypad(stdscr, true);
  nodelay(stdscr, true);  // non-blocking getchar()

  // Initialize all the colors. https://www.ditig.com/256-colors-cheat-sheet
  start_color();
  init_pair(Color::Debug, 246, COLOR_BLACK);  // #949494
  init_pair(Color::Yellow, 184, COLOR_BLACK);
  init_pair(Color::Red, COLOR_RED, COLOR_BLACK);
  init_pair(Color::BrightWhite, 15, COLOR_BLACK);
  init_pair(Color::Disengaged, COLOR_BLUE, COLOR_BLUE);
  init_pair(Color::Engaged, 28, 28);
  init_pair(Color::Green, 34, COLOR_BLACK);

  initWindows();

  qRegisterMetaType<uint64_t>("uint64_t");
  qRegisterMetaType<ReplyMsgType>("ReplyMsgType");
  installMessageHandler([this](ReplyMsgType type, const std::string msg) {
    emit logMessageSignal(type, QString::fromStdString(msg));
  });
  installDownloadProgressHandler([this](uint64_t cur, uint64_t total, bool success) {
    emit updateProgressBarSignal(cur, total, success);
  });

  QObject::connect(replay, &Replay::streamStarted, this, &ConsoleUI::updateSummary);
  QObject::connect(&notifier, SIGNAL(activated(int)), SLOT(readyRead()));
  QObject::connect(this, &ConsoleUI::updateProgressBarSignal, this, &ConsoleUI::updateProgressBar);
  QObject::connect(this, &ConsoleUI::logMessageSignal, this, &ConsoleUI::logMessage);

  sm_timer.callOnTimeout(this, &ConsoleUI::updateStatus);
  sm_timer.start(100);
  getch_timer.start(1000, this);
  readyRead();
}

ConsoleUI::~ConsoleUI() {
  endwin();
}

void ConsoleUI::initWindows() {
  getmaxyx(stdscr, max_height, max_width);
  w.fill(nullptr);
  w[Win::Title] = newwin(1, max_width, 0, 0);
  w[Win::Stats] = newwin(2, max_width - 2 * BORDER_SIZE, 2, BORDER_SIZE);
  w[Win::Timeline] = newwin(4, max_width - 2 * BORDER_SIZE, 5, BORDER_SIZE);
  w[Win::TimelineDesc] = newwin(1, 100, 10, BORDER_SIZE);
  w[Win::CarState] = newwin(3, 100, 12, BORDER_SIZE);
  w[Win::DownloadBar] = newwin(1, 100, 16, BORDER_SIZE);
  if (int log_height = max_height - 27; log_height > 4) {
    w[Win::LogBorder] = newwin(log_height, max_width - 2 * (BORDER_SIZE - 1), 17, BORDER_SIZE - 1);
    box(w[Win::LogBorder], 0, 0);
    w[Win::Log] = newwin(log_height - 2, max_width - 2 * BORDER_SIZE, 18, BORDER_SIZE);
    scrollok(w[Win::Log], true);
  }
  w[Win::Help] = newwin(5, max_width - (2 * BORDER_SIZE), max_height - 6, BORDER_SIZE);

  // set the title bar
  wbkgd(w[Win::Title], A_REVERSE);
  mvwprintw(w[Win::Title], 0, 3, "openpilot replay %s", COMMA_VERSION);

  // show windows on the real screen
  refresh();
  displayTimelineDesc();
  displayHelp();
  updateSummary();
  updateTimeline();
  for (auto win : w) {
    if (win) wrefresh(win);
  }
}

void ConsoleUI::timerEvent(QTimerEvent *ev) {
  if (ev->timerId() != getch_timer.timerId()) return;

  if (is_term_resized(max_height, max_width)) {
    for (auto win : w) {
      if (win) delwin(win);
    }
    endwin();
    clear();
    refresh();
    initWindows();
    rWarning("resize term %dx%d", max_height, max_width);
  }
  updateTimeline();
}

void ConsoleUI::updateStatus() {
  auto write_item = [this](int y, int x, const char *key, const std::string &value, const char *unit,
                           bool bold = false, Color color = Color::BrightWhite) {
    auto win = w[Win::CarState];
    wmove(win, y, x);
    add_str(win, key);
    add_str(win, value.c_str(), color, bold);
    add_str(win, unit);
  };
  static const std::pair<const char *, Color> status_text[] = {
      {"loading...", Color::Red},
      {"playing", Color::Green},
      {"paused...", Color::Yellow},
  };

  sm.update(0);

  if (status != Status::Paused) {
    status = (sm.updated("carState") || sm.updated("liveParameters")) ? Status::Playing : Status::Waiting;
  }
  auto [status_str, status_color] = status_text[status];
  write_item(0, 0, "STATUS:    ", status_str, "      ", false, status_color);
  std::string suffix = util::string_format(" / %s [%d/%d]      ", format_seconds(replay->totalSeconds()).c_str(),
                                           replay->currentSeconds() / 60, replay->route()->segments().size());
  write_item(0, 25, "TIME:  ", format_seconds(replay->currentSeconds()), suffix.c_str(), true);

  auto p = sm["liveParameters"].getLiveParameters();
  write_item(1, 0, "STIFFNESS: ", util::string_format("%.2f %%", p.getStiffnessFactor() * 100), "  ");
  write_item(1, 25, "SPEED: ", util::string_format("%.2f", sm["carState"].getCarState().getVEgo()), " m/s");
  write_item(2, 0, "STEER RATIO: ", util::string_format("%.2f", p.getSteerRatio()), "");
  auto angle_offsets = util::string_format("%.2f|%.2f", p.getAngleOffsetAverageDeg(), p.getAngleOffsetDeg());
  write_item(2, 25, "ANGLE OFFSET(AVG|INSTANT): ", angle_offsets, " deg");

  wrefresh(w[Win::CarState]);
}

void ConsoleUI::displayHelp() {
  for (int i = 0; i < std::size(keyboard_shortcuts); ++i) {
    wmove(w[Win::Help], i * 2, 0);
    for (auto &[key, desc] : keyboard_shortcuts[i]) {
      wattron(w[Win::Help], A_REVERSE);
      waddstr(w[Win::Help], (' ' + key + ' ').c_str());
      wattroff(w[Win::Help], A_REVERSE);
      waddstr(w[Win::Help], (' ' + desc + ' ').c_str());
    }
  }
  wrefresh(w[Win::Help]);
}

void ConsoleUI::displayTimelineDesc() {
  std::tuple<Color, const char *, bool> indicators[]{
      {Color::Engaged, " Engaged ", false},
      {Color::Disengaged, " Disengaged ", false},
      {Color::Green, " Info ", true},
      {Color::Yellow, " Warning ", true},
      {Color::Red, " Critical ", true},
  };
  for (auto [color, name, bold] : indicators) {
    add_str(w[Win::TimelineDesc], "__", color, bold);
    add_str(w[Win::TimelineDesc], name);
  }
}

void ConsoleUI::logMessage(ReplyMsgType type, const QString &msg) {
  if (auto win = w[Win::Log]) {
    Color color = Color::Default;
    if (type == ReplyMsgType::Debug) {
      color = Color::Debug;
    } else if (type == ReplyMsgType::Warning) {
      color = Color::Yellow;
    } else if (type == ReplyMsgType::Critical) {
      color = Color::Red;
    }
    add_str(win, qPrintable(msg + "\n"), color);
    wrefresh(win);
  }
}

void ConsoleUI::updateProgressBar(uint64_t cur, uint64_t total, bool success) {
  werase(w[Win::DownloadBar]);
  if (success && cur < total) {
    const int width = 35;
    const float progress = cur / (double)total;
    const int pos = width * progress;
    wprintw(w[Win::DownloadBar], "Downloading [%s>%s]  %d%% %s", std::string(pos, '=').c_str(),
            std::string(width - pos, ' ').c_str(), int(progress * 100.0), formattedDataSize(total).c_str());
  }
  wrefresh(w[Win::DownloadBar]);
}

void ConsoleUI::updateSummary() {
  const auto &route = replay->route();
  mvwprintw(w[Win::Stats], 0, 0, "Route: %s, %lu segments", qPrintable(route->name()), route->segments().size());
  mvwprintw(w[Win::Stats], 1, 0, "Car Fingerprint: %s", replay->carFingerprint().c_str());
  wrefresh(w[Win::Stats]);
}

void ConsoleUI::updateTimeline() {
  auto win = w[Win::Timeline];
  int width = getmaxx(win);
  werase(win);

  wattron(win, COLOR_PAIR(Color::Disengaged));
  mvwhline(win, 1, 0, ' ', width);
  mvwhline(win, 2, 0, ' ', width);
  wattroff(win, COLOR_PAIR(Color::Disengaged));

  const int total_sec = replay->totalSeconds();
  for (auto [begin, end, type] : replay->getTimeline()) {
    int start_pos = ((double)begin / total_sec) * width;
    int end_pos = ((double)end / total_sec) * width;
    if (type == TimelineType::Engaged) {
      mvwchgat(win, 1, start_pos, end_pos - start_pos + 1, A_COLOR, Color::Engaged, NULL);
      mvwchgat(win, 2, start_pos, end_pos - start_pos + 1, A_COLOR, Color::Engaged, NULL);
    } else {
      auto color_id = Color::Green;
      if (type != TimelineType::AlertInfo) {
        color_id = type == TimelineType::AlertWarning ? Color::Yellow : Color::Red;
      }
      mvwchgat(win, 3, start_pos, end_pos - start_pos + 1, ACS_S3, color_id, NULL);
    }
  }

  int cur_pos = ((double)replay->currentSeconds() / total_sec) * width;
  wattron(win, COLOR_PAIR(Color::BrightWhite));
  mvwaddch(win, 0, cur_pos, ACS_VLINE);
  mvwaddch(win, 3, cur_pos, ACS_VLINE);
  wattroff(win, COLOR_PAIR(Color::BrightWhite));
  wrefresh(win);
}

void ConsoleUI::readyRead() {
  int c;
  while ((c = getch()) != ERR) {
    handleKey(c);
  }
}

void ConsoleUI::pauseReplay(bool pause) {
  replay->pause(pause);
  status = pause ? Status::Paused : Status::Waiting;
}

void ConsoleUI::handleKey(char c) {
  if (c == '\n') {
    // pause the replay and blocking getchar()
    pauseReplay(true);
    updateStatus();
    getch_timer.stop();
    curs_set(true);
    nodelay(stdscr, false);

    // Wait for user input
    rWarning("Waiting for input...");
    int y = getmaxy(stdscr) - 9;
    move(y, BORDER_SIZE);
    add_str(stdscr, "Enter seek request: ", Color::BrightWhite, true);
    refresh();

    // Seek to choice
    echo();
    int choice = 0;
    scanw((char *)"%d", &choice);
    noecho();
    pauseReplay(false);
    replay->seekTo(choice, false);

    // Clean up and turn off the blocking mode
    move(y, 0);
    clrtoeol();
    nodelay(stdscr, true);
    curs_set(false);
    refresh();
    getch_timer.start(1000, this);

  } else if (c == 'x') {
    if (replay->hasFlag(REPLAY_FLAG_FULL_SPEED)) {
      replay->removeFlag(REPLAY_FLAG_FULL_SPEED);
      rWarning("replay at normal speed");
    } else {
      replay->addFlag(REPLAY_FLAG_FULL_SPEED);
      rWarning("replay at full speed");
    }
  } else if (c == 'e') {
    replay->seekToFlag(FindFlag::nextEngagement);
  } else if (c == 'd') {
    replay->seekToFlag(FindFlag::nextDisEngagement);
  } else if (c == 'm') {
    replay->seekTo(+60, true);
  } else if (c == 'M') {
    replay->seekTo(-60, true);
  } else if (c == 's') {
    replay->seekTo(+10, true);
  } else if (c == 'S') {
    replay->seekTo(-10, true);
  } else if (c == ' ') {
    pauseReplay(!replay->isPaused());
  } else if (c == 'q' || c == 'Q') {
    replay->stop();
    qApp->exit();
  }
}