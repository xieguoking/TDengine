/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// TAOS standard API example. The same syntax as MySQL, but only a subset
// to compile: gcc -o demo demo.c -ltaos

/**
 *  ts4125Check.c
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "taos.h"

typedef uint16_t VarDataLenT;

#define TSDB_NCHAR_SIZE    sizeof(int32_t)
#define VARSTR_HEADER_SIZE sizeof(VarDataLenT)

#define GET_FLOAT_VAL(x)  (*(float *)(x))
#define GET_DOUBLE_VAL(x) (*(double *)(x))

#define varDataLen(v) ((VarDataLenT *)(v))[0]

#define nDup         1
#define nRoot        10
#define nUser        10
#define USER_LEN     24
#define BUF_LEN      1024
#define RESULT_LIMIT 10000

#define QUERY_DB  "prod_index"
#define QUERY_STB "stb"
// #define QUERY_CTB "real_time_index_PARK11964_EMS01_METE_METE01_COSavg_0"

typedef struct {
  char     user[50];
  char     pass[50];
  char     host[50];
  int64_t  start;  // ms
  int64_t  end;    // ms
  int64_t  offset;
  int64_t  limit;
  int64_t  step;   // ms
  int64_t  span;   // ms
  uint32_t delay;  // second
  char     path[256];
} SCheckParams;

typedef struct {
  int64_t bizTime;
  int64_t updateTime;
} SQueryStash;

SCheckParams params = {
    .start = 1695139200000,  // 2023-09-20 00:00:00
    .end = 1696003200000,    // 2023-09-30 00:00:00
    .offset = 0,
    .limit = 5,
    .step = 8640000,
    .span = 86400000,
    .delay = 1,
    .host = "localhost",
    .user = "root",
    .pass = "taosdata",
};

SQueryStash stashArray[RESULT_LIMIT];
int32_t     stashNum = 0;

int32_t checkTables(TAOS *taos, char *qstr);
int32_t queryCheck(TAOS *taos, char *qstr, const char *tbName);

static int64_t getTimestampMs() {
  struct timeval systemTime;
  gettimeofday(&systemTime, NULL);
  return (int64_t)systemTime.tv_sec * 1000LL + (int64_t)systemTime.tv_usec / 1000;
}

static void queryDB(TAOS *taos, char *command) {
  int       i;
  TAOS_RES *pRes = NULL;
  int32_t   code = -1;

  for (i = 0; i < nDup; ++i) {
    if (NULL != pRes) {
      taos_free_result(pRes);
      pRes = NULL;
    }

    pRes = taos_query(taos, command);
    code = taos_errno(pRes);
    if (0 == code) {
      break;
    }
  }

  if (code != 0) {
    fprintf(stderr, "%" PRIi64 " failed to run: %s, reason: %s\n", getTimestampMs(), command, taos_errstr(pRes));
    taos_free_result(pRes);
    taos_close(taos);
    exit(EXIT_FAILURE);
  } else {
    // fprintf(stderr, "%" PRIi64 " success to run: %s\n", getTimestampMs(), command);
  }

  taos_free_result(pRes);
}

static void queryResults(TAOS *taos, char *command, TAOS_RES **result) {
  int       i;
  TAOS_RES *pRes = NULL;
  int32_t   code = -1;

  for (i = 0; i < nDup; ++i) {
    if (NULL != pRes) {
      taos_free_result(pRes);
      pRes = NULL;
    }

    pRes = taos_query(taos, command);
    code = taos_errno(pRes);
    if (0 == code) {
      break;
    }
  }

  if (code != 0) {
    fprintf(stderr, "%" PRIi64 " failed to run: %s, reason: %s\n", getTimestampMs(), command, taos_errstr(pRes));
    taos_free_result(pRes);
    taos_close(taos);
    *result = NULL;
    exit(EXIT_FAILURE);
  } else {
    *result = pRes;
    fprintf(stderr, "%" PRIi64 " success to run: %s\n", getTimestampMs(), command);
  }

  //   taos_free_result(pRes);
}

int printRow(char *str, TAOS_ROW row, TAOS_FIELD *fields, int numFields) {
  int  len = 0;
  char split = ' ';

  for (int i = 0; i < numFields; ++i) {
    if (i > 0) {
      str[len++] = split;
    }

    if (row[i] == NULL) {
      len += sprintf(str + len, "%s", "NULL");
      continue;
    }

    switch (fields[i].type) {
      case TSDB_DATA_TYPE_TINYINT:
        len += sprintf(str + len, "%d", *((int8_t *)row[i]));
        break;
      case TSDB_DATA_TYPE_UTINYINT:
        len += sprintf(str + len, "%u", *((uint8_t *)row[i]));
        break;
      case TSDB_DATA_TYPE_SMALLINT:
        len += sprintf(str + len, "%d", *((int16_t *)row[i]));
        break;
      case TSDB_DATA_TYPE_USMALLINT:
        len += sprintf(str + len, "%u", *((uint16_t *)row[i]));
        break;
      case TSDB_DATA_TYPE_INT:
        len += sprintf(str + len, "%d", *((int32_t *)row[i]));
        break;
      case TSDB_DATA_TYPE_UINT:
        len += sprintf(str + len, "%u", *((uint32_t *)row[i]));
        break;
      case TSDB_DATA_TYPE_BIGINT:
        len += sprintf(str + len, "%" PRId64, *((int64_t *)row[i]));
        break;
      case TSDB_DATA_TYPE_UBIGINT:
        len += sprintf(str + len, "%" PRIu64, *((uint64_t *)row[i]));
        break;
      case TSDB_DATA_TYPE_FLOAT: {
        float fv = 0;
        fv = GET_FLOAT_VAL(row[i]);
        len += sprintf(str + len, "%f", fv);
      } break;
      case TSDB_DATA_TYPE_DOUBLE: {
        double dv = 0;
        dv = GET_DOUBLE_VAL(row[i]);
        len += sprintf(str + len, "%lf", dv);
      } break;
      case TSDB_DATA_TYPE_BINARY:
      case TSDB_DATA_TYPE_VARBINARY:
      case TSDB_DATA_TYPE_NCHAR:
      case TSDB_DATA_TYPE_GEOMETRY: {
        int32_t charLen = varDataLen((char *)row[i] - VARSTR_HEADER_SIZE);
        memcpy(str + len, row[i], charLen);
        len += charLen;
      } break;
      case TSDB_DATA_TYPE_TIMESTAMP:
        len += sprintf(str + len, "%" PRId64, *((int64_t *)row[i]));
        if (i == 0) {
          stashArray[stashNum].bizTime = *((int64_t *)row[i]);
        } else if (i == 1) {
          stashArray[stashNum].updateTime = *((int64_t *)row[i]);
        }
        break;
      case TSDB_DATA_TYPE_BOOL:
        len += sprintf(str + len, "%d", *((int8_t *)row[i]));
      default:
        break;
    }
  }
  return len;
}

static int printResult(TAOS_RES *res, char *output) {
  int         numFields = taos_num_fields(res);
  TAOS_FIELD *fields = taos_fetch_fields(res);
#if PRINT_HEADER
  char header[BUF_LEN] = {0};
  int  len = 0;
  for (int i = 0; i < numFields; ++i) {
    len += sprintf(header + len, "%s ", fields[i].name);
  }
  puts(header);
  if (output) {
    strncpy(output, header, BUF_LEN);
  }
#endif

  TAOS_ROW row = NULL;
  while ((row = taos_fetch_row(res))) {
    char temp[BUF_LEN] = {0};
    printRow(temp, row, fields, numFields);

    ++stashNum;

    // puts(temp);
  }
}

static int fetchUpdateTime(TAOS_RES *res, int64_t *updateTime) {
  int         numFields = taos_num_fields(res);
  TAOS_FIELD *fields = taos_fetch_fields(res);

  TAOS_ROW row = NULL;
  while ((row = taos_fetch_row(res))) {
    assert(fields[1].type == TSDB_DATA_TYPE_TIMESTAMP);
    if (updateTime) *updateTime = *((int64_t *)row[1]);
    return 0;
  }
  return -1;
}

static void printHint() {
  printf("###################################################################################\n");
  printf("%s\n", " Utility to check biz_time/update_time for prod_index.real_time_index");
  printf("###################################################################################\n");
}

void printHelp() {
  char indent0[10] = "     ";
  char indent[10] = "        ";

  printHint();  // header

  printf("%s%s\n", indent0, "Usage sample:");
  printf("%s%s\n", indent,
         "./ts4125Check -user root -pass taosdata -start 2023-09-20 00:00:00 -end 2023-09-30 00:00:00 -offset 10 "
         "-limit 1000 -file /mytest/tables");
  printf("%s%s\n", indent0, "Params:");
  printf("%s%s\n", indent, "-host");
  printf("%s%s%s\n", indent, indent, "input the server host, default: localhost");
  printf("%s%s\n", indent, "-user");
  printf("%s%s%s\n", indent, indent, "input the user, default: root");
  printf("%s%s\n", indent, "-pass");
  printf("%s%s%s\n", indent, indent, "input the passwd, default: taosdata");
  printf("%s%s\n", indent, "-start");
  printf("%s%s%s\n", indent, indent, "input the start ms of query span, default: 2023-09-20 00:00:00");
  printf("%s%s\n", indent, "-end");
  printf("%s%s%s\n", indent, indent, "input the end ms of query span, default: 2023-09-30 00:00:00");
  printf("%s%s\n", indent, "-offset");
  printf("%s%s%s\n", indent, indent, "input the offset of query limit, default: 0");
  printf("%s%s\n", indent, "-limit");
  printf("%s%s%s%d%s\n", indent, indent, "input the query limit, range:[0,", RESULT_LIMIT, "], default: 5");
  printf("%s%s\n", indent, "-step");
  printf("%s%s%s\n", indent, indent, "input the query step, unit: ms, default: 8640000(1/10 day)");
  printf("%s%s\n", indent, "-span");
  printf("%s%s%s\n", indent, indent, "input the query span, unit: ms, default: 86400000(1 day)");
  printf("%s%s\n", indent, "-delay");
  printf("%s%s%s\n", indent, indent, "delay between each batch(step) of check, unit: second, range:[0,60], default: 1");
  printf("%s%s\n", indent, "-file");
  printf("%s%s%s\n", indent, indent,
         "input the full file path with child tables of stable 'real_time_index', e.g. show tables like "
         "\"prod_index.\" >> /mytest/tables");

  printHint();  // footer

  exit(EXIT_SUCCESS);
}

static void init(int argc, char **argv) {
  if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    printHelp();
    exit(EXIT_SUCCESS);
  }

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-start") == 0) {
      if (i < argc - 2) {
        char timeStr[20] = {0};
        strncpy(timeStr, argv[++i], 10);
        timeStr[10] = ' ';
        strncpy(&timeStr[11], argv[++i], 8);
        if (strlen(timeStr) != 19 || timeStr[4] != '-' || timeStr[7] != '-' || timeStr[13] != ':' ||
            timeStr[16] != ':') {
          fprintf(stderr, "start time format should be YYYY-MM-DD hh:mm:ss\n");
          exit(EXIT_FAILURE);
        }

        struct tm tm;
        memset(&tm, 0, sizeof(struct tm));
        char *str = strptime(timeStr, "%Y-%m-%d %H:%M:%S", &tm);
        if (str == NULL) {
          fprintf(stderr, "start time format should be valid YYYY-MM-DD hh:mm:ss\n");
          exit(EXIT_FAILURE);
        }

        int64_t startSec = mktime(&tm);
        params.start = startSec * 1000;
      } else if (i < argc - 1) {
        fprintf(stderr, "start time format should be: YYYY-MM-DD hh:mm:ss\n");
        exit(EXIT_FAILURE);
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    }

    else if (strcmp(argv[i], "-end") == 0) {
      if (i < argc - 2) {
        char timeStr[20] = {0};
        strncpy(timeStr, argv[++i], 10);
        timeStr[10] = ' ';
        strncpy(&timeStr[11], argv[++i], 8);
        if (strlen(timeStr) != 19 || timeStr[4] != '-' || timeStr[7] != '-' || timeStr[13] != ':' ||
            timeStr[16] != ':') {
          fprintf(stderr, "end time format should be YYYY-MM-DD hh:mm:ss\n");
          exit(EXIT_FAILURE);
        }

        struct tm tm;
        memset(&tm, 0, sizeof(struct tm));
        char *str = strptime(timeStr, "%Y-%m-%d %H:%M:%S", &tm);
        if (str == NULL) {
          fprintf(stderr, "end time format should be valid YYYY-MM-DD hh:mm:ss\n");
          exit(EXIT_FAILURE);
        }

        int64_t startSec = mktime(&tm);
        params.start = startSec * 1000;
      } else if (i < argc - 1) {
        fprintf(stderr, "end time format should be: YYYY-MM-DD hh:mm:ss\n");
        exit(EXIT_FAILURE);
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "-host") == 0) {
      if (i < argc - 1) {
        char *pstr = argv[++i];
        strncpy(params.host, pstr, 50);
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "-user") == 0) {
      if (i < argc - 1) {
        char *pstr = argv[++i];
        strncpy(params.user, pstr, 50);
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "-pass") == 0) {
      if (i < argc - 1) {
        char *pstr = argv[++i];
        strncpy(params.pass, pstr, 50);
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "-file") == 0) {
      if (i < argc - 1) {
        char *pstr = argv[++i];
        strncpy(params.path, pstr, 256);
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "-offset") == 0) {
      if (i < argc - 1) {
        char *pstr = argv[++i];

        int64_t offset = atoll(pstr);
        if (offset < 0) {
          fprintf(stderr, "offset should >= 0\n");
          exit(EXIT_FAILURE);
        }
        params.offset = offset;
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "-limit") == 0) {
      if (i < argc - 1) {
        char *pstr = argv[++i];

        int64_t limit = atoll(pstr);
        if (limit < 0 || limit > RESULT_LIMIT) {
          fprintf(stderr, "limit should between 0 and %d\n", RESULT_LIMIT);
          exit(EXIT_FAILURE);
        }
        params.limit = limit;
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "-span") == 0) {
      if (i < argc - 1) {
        char *pstr = argv[++i];

        int64_t span = atoll(pstr);
        if (span < 0) {
          fprintf(stderr, "step should > 0\n");
          exit(EXIT_FAILURE);
        }
        params.span = span;
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "-step") == 0) {
      if (i < argc - 1) {
        char *pstr = argv[++i];

        int64_t step = atoll(pstr);
        if (step < 0) {
          fprintf(stderr, "step should > 0\n");
          exit(EXIT_FAILURE);
        }
        params.step = step;
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "-delay") == 0) {
      if (i < argc - 1) {
        char *pstr = argv[++i];

        int64_t delay = atoll(pstr);
        if (delay < 0 || delay > 60) {
          fprintf(stderr, "delay should between 0 and 60\n");
          exit(EXIT_FAILURE);
        }
        params.delay = (uint32_t)delay;
      } else {
        fprintf(stderr, "option %s requires an argument\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    }
    // unknown params
    else if (i > 0) {
      fprintf(stderr, "unknown param: %s\n", argv[i]);
      exit(EXIT_FAILURE);
    }
  }

_return:
  if (params.end <= params.start) {
    fprintf(stderr, "start should less than end\n");
    exit(EXIT_FAILURE);
  }
  if (params.path[0] == 0) {
    fprintf(stderr, "file path should be specified\n");
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char *argv[]) {
  char qstr[1024];

  init(argc, argv);

  TAOS *taos = taos_connect(params.host, params.user, params.pass, NULL, 0);
  if (taos == NULL) {
    printf("failed to connect to server, reason:%s\n, host:%s, user:%s, pass:%s\n", "null taos", params.host,
           params.user, params.pass);
    exit(1);
  }

  checkTables(taos, qstr);

  taos_close(taos);
  taos_cleanup();
}

int64_t getsFile(FILE *fp, int32_t maxSize, char *__restrict buf) {
  if (fp == NULL || buf == NULL) {
    return -1;
  }
  if (fgets(buf, maxSize, fp) == NULL) {
    return -1;
  }
  return strlen(buf);
}

int32_t checkTables(TAOS *taos, char *qstr) {
  FILE *fp = fopen(params.path, "rb+");
  if (!fp) {
    printf("failed to open %s since %s\n", params.path, strerror(errno));
    return -1;
  }

  int32_t code = 0;
  char    line[1024];
  int64_t len;
  int64_t tLen;
  while (!feof(fp)) {
    line[0] = 0;
    len = getsFile(fp, sizeof(line), line);
    if (len <= 0 || len > 200) continue;
    if (line[0] == '#' || line[0] == ' ' || line[0] == '\n' || line[0] == '\r' || line[0] == '\t') continue;

    tLen = len;
    while (--tLen >= 0) {
      if (line[tLen] == ' ' || line[tLen] == '\t' || line[tLen] == '\n' || line[tLen] == '\r') {
        line[tLen] = 0;
      }
    }

    if (strncmp(line, "table_name", 50) == 0) continue;

    if ((code = queryCheck(taos, qstr, line)) != 0) {
      break;
    }
  }

  if (fp) {
    fflush(fp);
    fclose(fp);
    fp = NULL;
  }

  return code;
}

int32_t queryCheck(TAOS *taos, char *qstr, const char *tbName) {
  fprintf(stderr, "%" PRIi64 " check table %s\n", getTimestampMs(), tbName);

  int64_t startTS = params.start;
  int64_t endTS = params.end;
  int64_t step = params.step;
  int64_t span = params.span;

  for (int64_t ts = startTS; ts < endTS; ts += step) {
    sprintf(qstr, "select biz_time,update_time  from %s.`%s` where biz_time between %" PRIi64 " and %" PRIi64 ";",
            QUERY_DB, tbName, ts, ts + span);
    printf("%" PRIi64 " %s\n", getTimestampMs(), qstr);
    // query the result and put biz_time/update_time into array
    TAOS_RES *pRes = NULL;
    stashNum = 0;

    queryResults(taos, qstr, &pRes);
    if (pRes) {
      printResult(pRes, NULL);
      taos_free_result(pRes);
      printf("%" PRIi64 " total query results num is %d\n", getTimestampMs(), stashNum);
      for (int32_t i = 0; i < stashNum; ++i) {
        sprintf(qstr, "select biz_time,update_time  from %s.`%s` where biz_time=%" PRIi64 ";", QUERY_DB, tbName,
                stashArray[i].bizTime);
        TAOS_RES *qRes = NULL;
        queryResults(taos, qstr, &qRes);
        if (!qRes) {
          int64_t updateTime = INT64_MIN;
          fetchUpdateTime(qRes, &updateTime);
          if (updateTime != stashArray[i].updateTime) {
            
            // save output and return 0
            taos_free_result(qRes);
            return -1;
          }
          taos_free_result(qRes);
        }
      }
    }
    // sleep between each step
    if (params.delay > 0) {
      sleep(params.delay);
    }
  }
  return 0;
}