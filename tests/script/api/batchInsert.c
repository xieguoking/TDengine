// TAOS standard API example. The same syntax as MySQL, but only a subet 
// to compile: gcc -o prepare prepare.c -ltaos

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include "../../../include/client/taos.h"

typedef struct BiThreadParam {
  TAOS     *taos;
  int64_t   startTs;
  int64_t   rowNum;
  int32_t   tIdx;
} BiThreadParam;

#define BI_CREATE_THREAD_NUM 10
#define BI_INSERT_THREAD_NUM 5
#define BI_MAX_THREAD_NUM 10

int64_t biTs = 1591000000000;
int32_t biMaxSqlLen = 1048576;
int64_t biTbNum = 20000;
int64_t biRowNumPerTb = 10000;
double  biThreadRec[BI_MAX_THREAD_NUM] = {0};

int32_t taosGetTimeOfDay(struct timeval *tv) {
  return gettimeofday(tv, NULL);
}
void *taosMemoryMalloc(uint64_t size) {
  return malloc(size);
}

void *taosMemoryCalloc(int32_t num, int32_t size) {
  return calloc(num, size);
}
void taosMemoryFree(const void *ptr) {
  if (ptr == NULL) return;

  return free((void*)ptr);
}

static int64_t taosGetTimestampMs() {
  struct timeval systemTime;
  taosGetTimeOfDay(&systemTime);
  return (int64_t)systemTime.tv_sec * 1000LL + (int64_t)systemTime.tv_usec/1000;
}

static int64_t taosGetTimestampUs() {
  struct timeval systemTime;
  taosGetTimeOfDay(&systemTime);
  return (int64_t)systemTime.tv_sec * 1000000LL + (int64_t)systemTime.tv_usec;
}

void biFetchRows(TAOS_RES *result, bool printr, int32_t *rows) {
  TAOS_ROW    row;
  int         num_fields = taos_num_fields(result);
  TAOS_FIELD *fields = taos_fetch_fields(result);
  char        temp[256];

  // fetch the records row by row
  while ((row = taos_fetch_row(result))) {
    (*rows)++;
    if (printr) {
      memset(temp, 0, sizeof(temp));
      taos_print_row(temp, row, fields, num_fields);
      printf("\t[%s]\n", temp);
    }
  }
}

void biExecQuery(TAOS    * taos, char* sql, bool printr, int32_t *rows) {
  TAOS_RES *result = taos_query(taos, sql);
  int code = taos_errno(result);
  if (code != 0) {
    printf("!!!failed to query table, reason:%s\n", taos_errstr(result));
    taos_free_result(result);
    exit(1);
  }

  biFetchRows(result, printr, rows);

  taos_free_result(result);
}

void biPrepareDBSTb(TAOS     *taos) {
  TAOS_RES *result;
  int      code;

  result = taos_query(taos, "drop database if exists demo"); 
  code = taos_errno(result);
  if (code != 0) {
    printf("!!!failed to drop database, reason:%s\n", taos_errstr(result));
    taos_free_result(result);
    exit(1);
  }
  taos_free_result(result);
  
  result = taos_query(taos, "create database demo buffer 180 maxRows 10000 minRows 10 pages 1024 pagesize 16 vgroups 100 stt_trigger 16");
  code = taos_errno(result);
  if (code != 0) {
    printf("!!!failed to create database, reason:%s\n", taos_errstr(result));
    taos_free_result(result);
    exit(1);
  }
  taos_free_result(result);

  result = taos_query(taos, "use demo");
  taos_free_result(result);

  result = taos_query(taos, "create stable st1 (ts timestamp, f1 int) tags (tg1 int)");
  code = taos_errno(result);
  if (code != 0) {
    printf("!!!failed to create stb, reason:%s\n", taos_errstr(result));
    taos_free_result(result);
    exit(1);
  }
  taos_free_result(result);
}

void biPrepareTbs(TAOS     *taos, int32_t tIdx, int32_t tbNum) {
  TAOS_RES *result;
  int      code;

  printf("Thread %d start to create %d table\n", tIdx, tbNum);
  char *sql = taosMemoryMalloc(biMaxSqlLen);
  int32_t sqlLen = sprintf(sql, "create table ");
  int32_t endTb = (tIdx + 1) * tbNum;
  for (int i = tIdx * tbNum; i < endTb; i++) {
    sqlLen += sprintf(sql + sqlLen, "t%.10d using st1 tags(%d) ", i, i);
    if (sqlLen > (biMaxSqlLen - 300)) {
      result = taos_query(taos, sql);
      code = taos_errno(result);
      if (code != 0) {
        printf("!!!failed to create table [%s], reason:%s\n", sql, taos_errstr(result));
        taos_free_result(result);
        exit(1);
      }
      taos_free_result(result);
      if ((i + 1) < endTb) {
        sqlLen = sprintf(sql, "create table ");
      } else {
        sql[0] = 0;
        break;
      }
    }
  }
  if (sql[0]) {
    result = taos_query(taos, sql);
    code = taos_errno(result);
    if (code != 0) {
      printf("!!!failed to create table [%s], reason:%s\n", sql, taos_errstr(result));
      taos_free_result(result);
      exit(1);
    }
    taos_free_result(result);
  }
  
  printf("Thread %d finish create table\n", tIdx);

  taosMemoryFree(sql);
}

void biInsertTbs(TAOS     *taos, int32_t tIdx, int64_t tbNum, int64_t rowNum, int64_t startTs) {
  TAOS_RES *result;
  int      code;
  int64_t  st, used = 0;
  int32_t  times = 0;

  char *sql = taosMemoryMalloc(biMaxSqlLen);
  int32_t sqlLen = sprintf(sql, "insert into ");

  for (uint32_t r = 0; r < rowNum; ++r) {
    for (int i = 0 ; i < tbNum; i++) {
      sqlLen += sprintf(sql + sqlLen, "t%.10d values(%" PRId64 ", %d) ", i, startTs++, i);
      if (sqlLen > (biMaxSqlLen - 300)) {
        st = taosGetTimestampUs();
        result = taos_query(taos, sql);
        used += taosGetTimestampUs() - st;
        code = taos_errno(result);
        if (code != 0) {
          printf("!!!failed to insert data, reason:%s\n", taos_errstr(result));
          taos_free_result(result);
          exit(1);
        }
        taos_free_result(result);
        times++;
        if ((i + 1) < tbNum || (r + 1) < rowNum) {
          sqlLen = sprintf(sql, "insert into ");
        } else {
          sql[0] = 0;
        }
      }
    }
  }

  if (sql[0]) {
    st = taosGetTimestampUs();
    result = taos_query(taos, sql);
    used += taosGetTimestampUs() - st;
    code = taos_errno(result);
    if (code != 0) {
      printf("!!!failed to insert data, reason:%s\n", taos_errstr(result));
      taos_free_result(result);
      exit(1);
    }
    taos_free_result(result);
    times++;
  }

  taosMemoryFree(sql);
  used /= 1000000;
  biThreadRec[tIdx] = (double)(rowNum * tbNum / used);
  printf("Thread %d finish insert, exec times:%d, avg record:%d, insert rows:%d, usedSecond:%d\n", 
    tIdx, times, rowNum * tbNum / times, rowNum * tbNum, used);
}

void *biInsertThreadFp(void *arg) {
  BiThreadParam* tParam = (BiThreadParam*)arg;
  biInsertTbs(tParam->taos, tParam->tIdx, biTbNum, tParam->rowNum, tParam->startTs);
}

void *biCreateTbThreadFp(void *arg) {
  BiThreadParam* tParam = (BiThreadParam*)arg;
  biPrepareTbs(tParam->taos, tParam->tIdx, (int32_t)(biTbNum / BI_CREATE_THREAD_NUM));
}

void biRun(TAOS     *taos) {
  pthread_t qid[BI_MAX_THREAD_NUM];
  BiThreadParam params[BI_MAX_THREAD_NUM];
  double avgSpeed = 0;

  biPrepareDBSTb(taos);

  for (int t = 0; t < BI_CREATE_THREAD_NUM; t++) {
    params[t].taos = taos;
    params[t].tIdx = t;
    pthread_create(&qid[t], NULL, biCreateTbThreadFp, (void*)&params[t]);
  }

  for (int t = 0; t < BI_CREATE_THREAD_NUM; t++) {
    pthread_join(qid[t], NULL);
  }

  printf("all tables created\n");
  sleep(10);
  
  
  for (int t = 0; t < BI_INSERT_THREAD_NUM; t++) {
    params[t].taos = taos;
    params[t].startTs = biTs + t * biRowNumPerTb / BI_INSERT_THREAD_NUM * biTbNum;
    params[t].rowNum = biRowNumPerTb / BI_INSERT_THREAD_NUM;
    params[t].tIdx = t;
    pthread_create(&qid[t], NULL, biInsertThreadFp, (void*)&params[t]);
  }

  for (int t = 0; t < BI_INSERT_THREAD_NUM; t++) {
    pthread_join(qid[t], NULL);
  }

  printf("maxSqlLen %d - thread speed:", biMaxSqlLen);
  for (int t = 0; t < BI_INSERT_THREAD_NUM; t++) {
    printf("%fr/s ", biThreadRec[t]);
    avgSpeed += biThreadRec[t];
  }
  printf("\nmaxSqlLen %d - avg speed:%fr/s\n", biMaxSqlLen, avgSpeed/BI_INSERT_THREAD_NUM);

}

int main(int argc, char *argv[])
{
  TAOS     *taos = NULL;

  srand((unsigned int)time(NULL));
  
  // connect to server
  if (argc < 2) {
    printf("please input server ip \n");
    return 0;
  }

  taos = taos_connect(argv[1], "root", "taosdata", NULL, 0);
  if (taos == NULL) {
    printf("failed to connect to db, reason:%s\n", taos_errstr(taos));
    exit(1);
  }   


  biRun(taos);

  biMaxSqlLen = 10485760;
  biRun(taos);

  //biMaxSqlLen = 104857600;
  //biRun(taos);

  taos_close(taos);

  return 0;
}

