#ifndef RCPP_DATE_TIME_H_
#define RCPP_DATE_TIME_H_

#include <ctime>
#include <stdlib.h>
#include "DateTimeLocale.h"
#include "TzManager.h"

// Much of this code is adapted from R's src/main/datetime.c.
// Author: The R Core Team.
// License: GPL >= 2

static const int month_length[12] =
  {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const int month_start[12] =
  {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

// Leap days occur in a 400 year cycle: this records the cumulative number
// of leap days in per cycle. Generated with:
// is_leap <- function(y) (y %% 4) == 0 & ((y %% 100) != 0 | (y %% 400) == 0)
// cumsum(is_leap(0:399))
static const int leap_days[400] =
  {0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7,
   7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12,
   12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17,
   17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22,
   22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25,
   26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30,
   30, 31, 31, 31, 31, 32, 32, 32, 32, 33, 33, 33, 33, 34, 34, 34, 34, 35, 35,
   35, 35, 36, 36, 36, 36, 37, 37, 37, 37, 38, 38, 38, 38, 39, 39, 39, 39, 40,
   40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43, 43, 44, 44, 44, 44,
   45, 45, 45, 45, 46, 46, 46, 46, 47, 47, 47, 47, 48, 48, 48, 48, 49, 49, 49,
   49, 49, 49, 49, 49, 50, 50, 50, 50, 51, 51, 51, 51, 52, 52, 52, 52, 53, 53,
   53, 53, 54, 54, 54, 54, 55, 55, 55, 55, 56, 56, 56, 56, 57, 57, 57, 57, 58,
   58, 58, 58, 59, 59, 59, 59, 60, 60, 60, 60, 61, 61, 61, 61, 62, 62, 62, 62,
   63, 63, 63, 63, 64, 64, 64, 64, 65, 65, 65, 65, 66, 66, 66, 66, 67, 67, 67,
   67, 68, 68, 68, 68, 69, 69, 69, 69, 70, 70, 70, 70, 71, 71, 71, 71, 72, 72,
   72, 72, 73, 73, 73, 73, 73, 73, 73, 73, 74, 74, 74, 74, 75, 75, 75, 75, 76,
   76, 76, 76, 77, 77, 77, 77, 78, 78, 78, 78, 79, 79, 79, 79, 80, 80, 80, 80,
   81, 81, 81, 81, 82, 82, 82, 82, 83, 83, 83, 83, 84, 84, 84, 84, 85, 85, 85,
   85, 86, 86, 86, 86, 87, 87, 87, 87, 88, 88, 88, 88, 89, 89, 89, 89, 90, 90,
   90, 90, 91, 91, 91, 91, 92, 92, 92, 92, 93, 93, 93, 93, 94, 94, 94, 94, 95,
   95, 95, 95, 96, 96, 96, 96, 97, 97, 97};

static const int cycle_days = 400 * 365 + 97;

inline int is_leap(unsigned y) {
  return (y % 4) == 0 && ((y % 100) != 0 || (y % 400) == 0);
}

class DateTime {
  int year_, mon_, day_, hour_, min_, sec_, offset_;
  double psec_;
  std::string tz_;

public:
  DateTime(int year, int mon, int day, int hour = 0, int min = 0, int sec = 0,
           double psec = 0, const std::string& tz = ""):
      year_(year), mon_(mon), day_(day), hour_(hour), min_(min), sec_(sec),
      offset_(0), psec_(psec), tz_(tz) {
  }

  // Used to add time zone offsets which can only be easily applied once
  // we've converted into seconds since epoch.
  void setOffset(int offset) {
    offset_ = offset;
  }

  // Is this a valid date time?
  bool isValid() const {
    if (sec_ < 0 || sec_ > 60)
      return false;
    if (min_ < 0 || min_ > 59)
      return false;
    if (hour_ < 0 || hour_ > 23)
      return false;
    if (mon_ < 0 || mon_ > 11)
      return false;
    if (day_ < 0 || day_ >= days_in_month())
      return false;

    return true;
  }

  // Adjust a struct tm to be a valid date-time.
  // Return 0 if valid, -1 if invalid and uncorrectable, or a positive
  // integer approximating the number of corrections needed.
  int repair() {
    int tmp, fixes = 0;

    if (sec_ < 0 || sec_ > 60) { /* 61 POSIX, 60 draft ISO C */
      fixes++;
      int extra_mins = sec_ / 60;
      sec_ -= 60 * extra_mins;
      min_ += extra_mins;
      if (sec_ < 0) {
        sec_ += 60;
        min_--;
      }
    }

    fixes += repair_hour_min();

    /* defer fixing mday until we know the year */
    if (mon_ < 0 || mon_ > 11) {
      fixes++;
      tmp = mon_/12;
      mon_ -= 12 * tmp;
      year_ += tmp;

      if(mon_ < 0) {
        mon_ += 12;
        year_--;
      }
    }

    // Never fix more than 10 years worth of days
    if (abs(day_) > 366 * 10)
      return -1;

    if (abs(day_) > 366) {
      fixes++;
      // first spin back until January
      while(mon_ > 0) {
        --mon_;
        day_ += days_in_month();
      }
      // then spin years
      while(day_ < 1) {
        year_--;
        day_ += days_in_year();
      }
      while(day_ > (tmp = days_in_year())) {
        year_++;
        day_ -= tmp;
      }
    }

    while(day_ < 1) {
      fixes++;
      mon_--;
      if(mon_ < 0) {
        mon_ += 12;
        year_--;
      }
      day_ += days_in_month();
    }

    while(day_ > (tmp = days_in_month())) {
      fixes++;
      mon_++;
      if(mon_ > 11) {
        mon_ -= 12;
        year_++;
      }
      day_ -= tmp;
    }
    return fixes;
  }

  double time(TzManager* pTzManager) const {
    return (tz_ == "UTC") ? utctime() : localtime(pTzManager);
  }

  int date() const {
    return utcdate();
  }

private:

  int repair_hour_min() {
    int fixes = 0, tmp = 0;

    if (min_ < 0 || min_ > 59) {
      fixes++;
      tmp = min_ / 60;
      min_ -= 60 * tmp;
      hour_ += tmp;
      if (min_ < 0) {
        min_ += 60;
        hour_--;
      }
    }

    if (hour_ == 24 && min_ == 0 && sec_ == 0) {
      hour_ = 0;
      day_++;

      if (mon_ >= 0 && mon_ <= 11) {
        if (day_ > days_in_month()) {
          mon_++;
          day_ = 1;
          if (min_ == 12) {
            year_++;
            mon_ = 0;
          }
        }
      }
    }

    if (hour_ < 0 || hour_ > 23) {
      fixes++;
      tmp = hour_ / 24;
      hour_ -= 24 * tmp;
      day_ += tmp;
      if(hour_ < 0) {
        hour_ += 24;
        day_--;
      }
    }

    return fixes;
  }

  // Number of number of seconds since 1970-01-01T00:00:00Z.
  // Compared to usual implementations this returns a double, and supports
  // a wider range of dates. Invalid dates have undefined behaviour.
  double utctime() const {
    return offset_ + psec_ + sec_ + (min_ * 60) + (hour_ * 3600) +
      (utcdate() * 86400.0);
  }

  // Find number of days since 1970-01-01.
  // Invalid dates have undefined behaviour.
  int utcdate() const {
    if (!isValid())
      return NA_REAL;

    // Number of days since start of year
    int day = month_start[mon_] + day_;
    if (mon_ > 1 && is_leap(year_))
      day++;

    // Number of days since 0000-01-01
    // Leap years come in 400 year cycles so determine which cycle we're
    // in, and what position we're in within that cycle.
    int ly_cycle = year_ / 400;
    int ly_offset = year_ - (ly_cycle * 400);
    if (ly_offset < 0) {
      ly_offset += 400;
      ly_cycle--;
    }
    day += ly_cycle * cycle_days + ly_offset * 365 + leap_days[ly_offset];

    // Convert to number of days since 1970-01-01
    day -= 719528;

    return day;
  }


  double localtime(TzManager* pTzManager) const {
    pTzManager->setTz(tz_);
    if (!isValid())
      return NA_REAL;

    struct tm tm;
    tm.tm_year = year_ - 1900;
    tm.tm_mon = mon_;
    tm.tm_mday = day_ + 1;
    tm.tm_hour = hour_;
    tm.tm_min = min_;
    tm.tm_sec = sec_;

    time_t time = mktime(&tm);
    return time + psec_ + offset_;
  }

  inline int days_in_month() const {
    return month_length[mon_] + (mon_ == 1 && is_leap(year_));
  }
  inline int days_in_year() const {
    return 365 + is_leap(year_);
  }
};

#endif
