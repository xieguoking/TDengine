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
#include "tsdb.h"
#include "tsdbDataFileRW.h"
#include "tsdbReadUtil.h"
#include "vnd.h"

static int32_t getTableDelDataFromDelIdx(SDelFReader *pDelReader, SDelIdx *pDelIdx, SArray *aDelData) {
  int32_t code = 0;

  if (pDelIdx) {
    code = tsdbReadDelDatav1(pDelReader, pDelIdx, aDelData, INT64_MAX);
  }

  return code;
}

static int32_t getTableDelDataFromTbData(STbData *pTbData, SArray *aDelData) {
  int32_t   code = 0;
  SDelData *pDelData = pTbData ? pTbData->pHead : NULL;

  for (; pDelData; pDelData = pDelData->pNext) {
    taosArrayPush(aDelData, pDelData);
  }

  return code;
}

static int32_t getTableDelData(STbData *pMem, STbData *pIMem, SDelFReader *pDelReader, SDelIdx *pDelIdx,
                               SArray *aDelData) {
  int32_t code = 0;

  if (pDelIdx) {
    code = getTableDelDataFromDelIdx(pDelReader, pDelIdx, aDelData);
    if (code) goto _err;
  }

  if (pMem) {
    code = getTableDelDataFromTbData(pMem, aDelData);
    if (code) goto _err;
  }

  if (pIMem) {
    code = getTableDelDataFromTbData(pIMem, aDelData);
    if (code) goto _err;
  }

_err:
  return code;
}

static int32_t getTableDelSkyline(STbData *pMem, STbData *pIMem, SDelFReader *pDelReader, SDelIdx *pDelIdx,
                                  SArray *aSkyline) {
  int32_t code = 0;
  SArray *aDelData = NULL;

  aDelData = taosArrayInit(32, sizeof(SDelData));
  code = getTableDelData(pMem, pIMem, pDelReader, pDelIdx, aDelData);
  if (code) goto _err;

  size_t nDelData = taosArrayGetSize(aDelData);
  if (nDelData > 0) {
    code = tsdbBuildDeleteSkyline(aDelData, 0, (int32_t)(nDelData - 1), aSkyline);
    if (code) goto _err;
  }

_err:
  if (aDelData) {
    taosArrayDestroy(aDelData);
  }
  return code;
}

static int32_t loadTombFromBlk(const TTombBlkArray *pTombBlkArray, SCacheRowsReader *pReader, void *pFileReader,
                               bool isFile) {
  int32_t   code = 0;
  uint64_t *uidList = pReader->uidList;
  int32_t   numOfTables = pReader->numOfTables;
  int64_t   suid = pReader->info.suid;

  for (int i = 0, j = 0; i < pTombBlkArray->size && j < numOfTables; ++i) {
    STombBlk *pTombBlk = &pTombBlkArray->data[i];
    if (pTombBlk->maxTbid.suid < suid || (pTombBlk->maxTbid.suid == suid && pTombBlk->maxTbid.uid < uidList[0])) {
      continue;
    }

    if (pTombBlk->minTbid.suid > suid ||
        (pTombBlk->minTbid.suid == suid && pTombBlk->minTbid.uid > uidList[numOfTables - 1])) {
      break;
    }

    STombBlock block = {0};
    code = isFile ? tsdbDataFileReadTombBlock(pFileReader, &pTombBlkArray->data[i], &block)
                  : tsdbSttFileReadTombBlock(pFileReader, &pTombBlkArray->data[i], &block);
    if (code != TSDB_CODE_SUCCESS) {
      return code;
    }

    uint64_t        uid = uidList[j];
    STableLoadInfo *pInfo = *(STableLoadInfo **)tSimpleHashGet(pReader->pTableMap, &uid, sizeof(uid));
    if (pInfo->pTombData == NULL) {
      pInfo->pTombData = taosArrayInit(4, sizeof(SDelData));
    }

    STombRecord record = {0};
    bool        finished = false;
    for (int32_t k = 0; k < TARRAY2_SIZE(block.suid); ++k) {
      code = tTombBlockGet(&block, k, &record);
      if (code != TSDB_CODE_SUCCESS) {
        finished = true;
        break;
      }

      if (record.suid < suid) {
        continue;
      }
      if (record.suid > suid) {
        finished = true;
        break;
      }

      bool newTable = false;
      if (uid < record.uid) {
        while (j < numOfTables && uidList[j] < record.uid) {
          ++j;
          newTable = true;
        }

        if (j >= numOfTables) {
          finished = true;
          break;
        }

        uid = uidList[j];
      }

      if (record.uid < uid) {
        continue;
      }

      if (newTable) {
        pInfo = *(STableLoadInfo **)tSimpleHashGet(pReader->pTableMap, &uid, sizeof(uid));
        if (pInfo->pTombData == NULL) {
          pInfo->pTombData = taosArrayInit(4, sizeof(SDelData));
        }
      }

      if (record.version <= pReader->info.verRange.maxVer) {
        SDelData delData = {.version = record.version, .sKey = record.skey, .eKey = record.ekey};
        taosArrayPush(pInfo->pTombData, &delData);
      }
    }

    tTombBlockDestroy(&block);

    if (finished) {
      return code;
    }
  }

  return TSDB_CODE_SUCCESS;
}

static int32_t loadDataTomb(SCacheRowsReader *pReader, SDataFileReader *pFileReader) {
  int32_t code = 0;

  const TTombBlkArray *pBlkArray = NULL;
  code = tsdbDataFileReadTombBlk(pFileReader, &pBlkArray);
  if (code != TSDB_CODE_SUCCESS) {
    return code;
  }

  return loadTombFromBlk(pBlkArray, pReader, pFileReader, true);
}

static int32_t loadSttTomb(STsdbReader *pTsdbReader, SSttFileReader *pSttFileReader, SSttBlockLoadInfo *pLoadInfo) {
  int32_t code = 0;

  SCacheRowsReader *pReader = (SCacheRowsReader *)pTsdbReader;

  const TTombBlkArray *pBlkArray = NULL;
  code = tsdbSttFileReadTombBlk(pSttFileReader, &pBlkArray);
  if (code != TSDB_CODE_SUCCESS) {
    return code;
  }

  return loadTombFromBlk(pBlkArray, pReader, pSttFileReader, false);
}

typedef struct {
  SMergeTree  mergeTree;
  SMergeTree *pMergeTree;
} SFSLastIter;

static int32_t lastIterOpen(SFSLastIter *iter, STFileSet *pFileSet, STsdb *pTsdb, STSchema *pTSchema, tb_uid_t suid,
                            tb_uid_t uid, SCacheRowsReader *pr, int64_t lastTs, int16_t *aCols, int nCols) {
  int32_t code = 0;

  int64_t loadBlocks = 0;
  double  elapse = 0;
  pr->pLDataIterArray = destroySttBlockReader(pr->pLDataIterArray, &loadBlocks, &elapse);
  pr->pLDataIterArray = taosArrayInit(4, POINTER_BYTES);

  SMergeTreeConf conf = {
      .uid = uid,
      .suid = suid,
      .pTsdb = pTsdb,
      .timewindow = (STimeWindow){.skey = lastTs, .ekey = TSKEY_MAX},
      .verRange = (SVersionRange){.minVer = 0, .maxVer = UINT64_MAX},
      .strictTimeRange = false,
      .pSchema = pTSchema,
      .pCurrentFileset = pFileSet,
      .backward = 1,
      .pSttFileBlockIterArray = pr->pLDataIterArray,
      .pCols = aCols,
      .numOfCols = nCols,
      .loadTombFn = loadSttTomb,
      .pReader = pr,
      .idstr = pr->idstr,
  };

  code = tMergeTreeOpen2(&iter->mergeTree, &conf);
  if (code != TSDB_CODE_SUCCESS) {
    return -1;
  }

  iter->pMergeTree = &iter->mergeTree;

  return code;
}

static int32_t lastIterClose(SFSLastIter **iter) {
  int32_t code = 0;

  if ((*iter)->pMergeTree) {
    tMergeTreeClose((*iter)->pMergeTree);
    (*iter)->pMergeTree = NULL;
  }

  *iter = NULL;

  return code;
}

static int32_t lastIterNext(SFSLastIter *iter, TSDBROW **ppRow) {
  int32_t code = 0;

  bool hasVal = tMergeTreeNext(iter->pMergeTree);
  if (!hasVal) {
    *ppRow = NULL;
    return code;
  }

  *ppRow = tMergeTreeGetRow(iter->pMergeTree);

  return code;
}

typedef enum SFSNEXTROWSTATES {
  SFSNEXTROW_FS,
  SFSNEXTROW_FILESET,
  SFSNEXTROW_INDEXLIST,
  SFSNEXTROW_BRINBLOCK,
  SFSNEXTROW_BRINRECORD,
  SFSNEXTROW_BLOCKDATA,
  SFSNEXTROW_BLOCKROW
} SFSNEXTROWSTATES;

struct CacheNextRowIter;

typedef struct SFSNextRowIter {
  SFSNEXTROWSTATES         state;         // [input]
  STsdb                   *pTsdb;         // [input]
  SBlockIdx               *pBlockIdxExp;  // [input]
  STSchema                *pTSchema;      // [input]
  tb_uid_t                 suid;
  tb_uid_t                 uid;
  int32_t                  nFileSet;
  int32_t                  iFileSet;
  STFileSet               *pFileSet;
  TFileSetArray           *aDFileSet;
  SArray                  *pIndexList;
  int32_t                  iBrinIndex;
  SBrinBlock               brinBlock;
  int32_t                  iBrinRecord;
  SBrinRecord              brinRecord;
  SBlockData               blockData;
  SBlockData              *pBlockData;
  int32_t                  nRow;
  int32_t                  iRow;
  TSDBROW                  row;
  int64_t                  lastTs;
  SFSLastIter              lastIter;
  SFSLastIter             *pLastIter;
  TSDBROW                 *pLastRow;
  SRow                    *pTSRow;
  SRowMerger               rowMerger;
  SCacheRowsReader        *pr;
  struct CacheNextRowIter *pRowIter;
} SFSNextRowIter;

static void clearLastFileSet(SFSNextRowIter *state);

static int32_t getNextRowFromFS(void *iter, TSDBROW **ppRow, bool *pIgnoreEarlierTs, bool isLast, int16_t *aCols,
                                int nCols) {
  SFSNextRowIter *state = (SFSNextRowIter *)iter;
  int32_t         code = 0;

  if (SFSNEXTROW_FS == state->state) {
    state->nFileSet = TARRAY2_SIZE(state->aDFileSet);
    state->iFileSet = state->nFileSet;

    state->state = SFSNEXTROW_FILESET;
  }

  if (SFSNEXTROW_FILESET == state->state) {
  _next_fileset:
    if (--state->iFileSet < 0) {
      clearLastFileSet(state);

      *ppRow = NULL;
      return code;
    } else {
      state->pFileSet = TARRAY2_GET(state->aDFileSet, state->iFileSet);
    }

    STFileObj **pFileObj = state->pFileSet->farr;
    if (pFileObj[0] != NULL || pFileObj[3] != NULL) {
      SDataFileReaderConfig conf = {.tsdb = state->pTsdb, .szPage = state->pTsdb->pVnode->config.szPage};
      const char           *filesName[4] = {0};
      if (pFileObj[0] != NULL) {
        conf.files[0].file = *pFileObj[0]->f;
        conf.files[0].exist = true;
        filesName[0] = pFileObj[0]->fname;

        conf.files[1].file = *pFileObj[1]->f;
        conf.files[1].exist = true;
        filesName[1] = pFileObj[1]->fname;

        conf.files[2].file = *pFileObj[2]->f;
        conf.files[2].exist = true;
        filesName[2] = pFileObj[2]->fname;
      }

      if (pFileObj[3] != NULL) {
        conf.files[3].exist = true;
        conf.files[3].file = *pFileObj[3]->f;
        filesName[3] = pFileObj[3]->fname;
      }

      code = tsdbDataFileReaderOpen(filesName, &conf, &state->pr->pFileReader);
      if (code != TSDB_CODE_SUCCESS) {
        goto _err;
      }

      code = lastIterOpen(&state->lastIter, state->pFileSet, state->pTsdb, state->pTSchema, state->suid, state->uid,
                          state->pr, state->lastTs, aCols, nCols);
      if (code != TSDB_CODE_SUCCESS) {
        goto _err;
      }

      state->pLastIter = &state->lastIter;

      loadDataTomb(state->pr, state->pr->pFileReader);

      if (!state->pIndexList) {
        state->pIndexList = taosArrayInit(1, sizeof(SBrinBlk));
      } else {
        taosArrayClear(state->pIndexList);
      }
      const TBrinBlkArray *pBlkArray = NULL;

      int32_t code = tsdbDataFileReadBrinBlk(state->pr->pFileReader, &pBlkArray);
      if (code != TSDB_CODE_SUCCESS) {
        goto _err;
      }

      for (int i = TARRAY2_SIZE(pBlkArray) - 1; i >= 0; --i) {
        SBrinBlk *pBrinBlk = &pBlkArray->data[i];
        if (state->suid >= pBrinBlk->minTbid.suid && state->suid <= pBrinBlk->maxTbid.suid) {
          if (state->uid >= pBrinBlk->minTbid.uid && state->uid <= pBrinBlk->maxTbid.uid) {
            taosArrayPush(state->pIndexList, pBrinBlk);
          }
        } else if (state->suid > pBrinBlk->maxTbid.suid ||
                   (state->suid == pBrinBlk->maxTbid.suid && state->uid > pBrinBlk->maxTbid.uid)) {
          break;
        }
      }

      int indexSize = TARRAY_SIZE(state->pIndexList);
      if (indexSize <= 0) {
        // goto next fileset
        clearLastFileSet(state);
        goto _next_fileset;
      }

      state->state = SFSNEXTROW_INDEXLIST;
      state->iBrinIndex = indexSize;
    } else {  // empty fileset, goto next fileset
      // clearLastFileSet(state);
      goto _next_fileset;
    }
  }

  if (SFSNEXTROW_INDEXLIST == state->state) {
    SBrinBlk *pBrinBlk = NULL;
  _next_brinindex:
    if (--state->iBrinIndex < 0) {  // no index left, goto next fileset
      clearLastFileSet(state);
      goto _next_fileset;
    } else {
      pBrinBlk = taosArrayGet(state->pIndexList, state->iBrinIndex);
    }

    code = tsdbDataFileReadBrinBlock(state->pr->pFileReader, pBrinBlk, &state->brinBlock);
    if (code != TSDB_CODE_SUCCESS) {
      goto _err;
    }

    state->iBrinRecord = BRIN_BLOCK_SIZE(&state->brinBlock) - 1;
    state->state = SFSNEXTROW_BRINBLOCK;
  }

  if (SFSNEXTROW_BRINBLOCK == state->state) {
  _next_brinrecord:
    if (state->iBrinRecord < 0) {  // empty brin block, goto _next_brinindex
      tBrinBlockClear(&state->brinBlock);
      goto _next_brinindex;
    }
    code = tBrinBlockGet(&state->brinBlock, state->iBrinRecord, &state->brinRecord);
    if (code != TSDB_CODE_SUCCESS) {
      goto _err;
    }

    SBrinRecord *pRecord = &state->brinRecord;
    if (pRecord->uid != state->uid) {
      // TODO: goto next brin block early
      --state->iBrinRecord;
      goto _next_brinrecord;
    }

    state->state = SFSNEXTROW_BRINRECORD;
  }

  if (SFSNEXTROW_BRINRECORD == state->state) {
    SBrinRecord *pRecord = &state->brinRecord;

    if (!state->pBlockData) {
      state->pBlockData = &state->blockData;
      code = tBlockDataCreate(&state->blockData);
      if (code) goto _err;
    } else {
      tBlockDataReset(state->pBlockData);
    }

    if (aCols[0] == PRIMARYKEY_TIMESTAMP_COL_ID) {
      --nCols;
      ++aCols;
    }
    code = tsdbDataFileReadBlockDataByColumn(state->pr->pFileReader, pRecord, state->pBlockData, state->pTSchema, aCols,
                                             nCols);
    if (code != TSDB_CODE_SUCCESS) {
      goto _err;
    }

    state->nRow = state->blockData.nRow;
    state->iRow = state->nRow - 1;

    state->state = SFSNEXTROW_BLOCKROW;
  }

  if (SFSNEXTROW_BLOCKROW == state->state) {
    if (state->iRow < 0) {
      --state->iBrinRecord;
      goto _next_brinrecord;
    }

    state->row = tsdbRowFromBlockData(state->pBlockData, state->iRow);
    if (!state->pLastIter) {
      *ppRow = &state->row;
      --state->iRow;
      return code;
    }

    if (!state->pLastRow) {
      // get next row from fslast and process with fs row, --state->Row if select fs row
      code = lastIterNext(&state->lastIter, &state->pLastRow);
      if (code != TSDB_CODE_SUCCESS) {
        goto _err;
      }
    }

    if (!state->pLastRow) {
      lastIterClose(&state->pLastIter);

      *ppRow = &state->row;
      --state->iRow;
      return code;
    }

    // process state->pLastRow & state->row
    TSKEY rowTs = TSDBROW_TS(&state->row);
    TSKEY lastRowTs = TSDBROW_TS(state->pLastRow);
    if (lastRowTs > rowTs) {
      *ppRow = state->pLastRow;
      state->pLastRow = NULL;
      return code;
    } else if (lastRowTs < rowTs) {
      *ppRow = &state->row;
      --state->iRow;
      return code;
    } else {
      // TODO: merge rows and *ppRow = mergedRow
      SRowMerger *pMerger = &state->rowMerger;
      tsdbRowMergerInit(pMerger, state->pTSchema);

      code = tsdbRowMergerAdd(pMerger, &state->row, state->pTSchema);
      if (code != TSDB_CODE_SUCCESS) {
        goto _err;
      }
      code = tsdbRowMergerAdd(pMerger, state->pLastRow, state->pTSchema);
      if (code != TSDB_CODE_SUCCESS) {
        goto _err;
      }

      if (state->pTSRow) {
        taosMemoryFree(state->pTSRow);
        state->pTSRow = NULL;
      }

      code = tsdbRowMergerGetRow(pMerger, &state->pTSRow);
      if (code != TSDB_CODE_SUCCESS) {
        goto _err;
      }

      state->row = tsdbRowFromTSRow(TSDBROW_VERSION(&state->row), state->pTSRow);
      *ppRow = &state->row;
      --state->iRow;

      tsdbRowMergerClear(pMerger);

      return code;
    }
  }

_err:
  clearLastFileSet(state);

  *ppRow = NULL;

  return code;
}

int32_t clearNextRowFromFS(void *iter) {
  int32_t code = 0;

  SFSNextRowIter *state = (SFSNextRowIter *)iter;
  if (!state) {
    return code;
  }

  clearLastFileSet(state);

  return code;
}

typedef enum SMEMNEXTROWSTATES {
  SMEMNEXTROW_ENTER,
  SMEMNEXTROW_NEXT,
} SMEMNEXTROWSTATES;

typedef struct SMemNextRowIter {
  SMEMNEXTROWSTATES state;
  STbData          *pMem;  // [input]
  STbDataIter       iter;  // mem buffer skip list iterator
  int64_t           lastTs;
} SMemNextRowIter;

static int32_t getNextRowFromMem(void *iter, TSDBROW **ppRow, bool *pIgnoreEarlierTs, bool isLast, int16_t *aCols,
                                 int nCols) {
  SMemNextRowIter *state = (SMemNextRowIter *)iter;
  int32_t          code = 0;
  *pIgnoreEarlierTs = false;
  switch (state->state) {
    case SMEMNEXTROW_ENTER: {
      if (state->pMem != NULL) {
        if (state->pMem->maxKey <= state->lastTs) {
          *ppRow = NULL;
          *pIgnoreEarlierTs = true;
          return code;
        }
        tsdbTbDataIterOpen(state->pMem, NULL, 1, &state->iter);

        TSDBROW *pMemRow = tsdbTbDataIterGet(&state->iter);
        if (pMemRow) {
          *ppRow = pMemRow;
          state->state = SMEMNEXTROW_NEXT;

          return code;
        }
      }

      *ppRow = NULL;

      return code;
    }
    case SMEMNEXTROW_NEXT:
      if (tsdbTbDataIterNext(&state->iter)) {
        *ppRow = tsdbTbDataIterGet(&state->iter);

        return code;
      } else {
        *ppRow = NULL;

        return code;
      }
    default:
      ASSERT(0);
      break;
  }

_err:
  *ppRow = NULL;
  return code;
}

static bool tsdbKeyDeleted(TSDBKEY *key, SArray *pSkyline, int64_t *iSkyline) {
  bool deleted = false;
  while (*iSkyline > 0) {
    TSDBKEY *pItemBack = (TSDBKEY *)taosArrayGet(pSkyline, *iSkyline);
    TSDBKEY *pItemFront = (TSDBKEY *)taosArrayGet(pSkyline, *iSkyline - 1);

    if (key->ts > pItemBack->ts) {
      return false;
    } else if (key->ts >= pItemFront->ts && key->ts <= pItemBack->ts) {
      if (key->version <= pItemFront->version || (key->ts == pItemBack->ts && key->version <= pItemBack->version)) {
        // if (key->version <= pItemFront->version || key->version <= pItemBack->version) {
        return true;
      } else {
        if (*iSkyline > 1) {
          --*iSkyline;
        } else {
          return false;
        }
      }
    } else {
      if (*iSkyline > 1) {
        --*iSkyline;
      } else {
        return false;
      }
    }
  }

  return deleted;
}

typedef int32_t (*_next_row_fn_t)(void *iter, TSDBROW **ppRow, bool *pIgnoreEarlierTs, bool isLast, int16_t *aCols,
                                  int nCols);
typedef int32_t (*_next_row_clear_fn_t)(void *iter);

typedef struct {
  TSDBROW             *pRow;
  bool                 stop;
  bool                 next;
  bool                 ignoreEarlierTs;
  void                *iter;
  _next_row_fn_t       nextRowFn;
  _next_row_clear_fn_t nextRowClearFn;
} TsdbNextRowState;

typedef struct CacheNextRowIter {
  SArray           *pMemDelData;
  SArray           *pSkyline;
  int64_t           iSkyline;
  SBlockIdx         idx;
  SMemNextRowIter   memState;
  SMemNextRowIter   imemState;
  SFSNextRowIter    fsState;
  TSDBROW           memRow, imemRow, fsLastRow, fsRow;
  TsdbNextRowState  input[3];
  SCacheRowsReader *pr;
  STsdb            *pTsdb;
} CacheNextRowIter;

static void clearLastFileSet(SFSNextRowIter *state) {
  if (state->pLastIter) {
    lastIterClose(&state->pLastIter);
  }

  if (state->pBlockData) {
    tBlockDataDestroy(state->pBlockData);
    state->pBlockData = NULL;
  }

  if (state->pr->pFileReader) {
    tsdbDataFileReaderClose(&state->pr->pFileReader);
    state->pr->pFileReader = NULL;
  }

  if (state->pTSRow) {
    taosMemoryFree(state->pTSRow);
    state->pTSRow = NULL;
  }

  if (state->pRowIter->pSkyline) {
    taosArrayDestroy(state->pRowIter->pSkyline);
    state->pRowIter->pSkyline = NULL;

    void   *pe = NULL;
    int32_t iter = 0;
    while ((pe = tSimpleHashIterate(state->pr->pTableMap, pe, &iter)) != NULL) {
      STableLoadInfo *pInfo = *(STableLoadInfo **)pe;
      pInfo->pTombData = taosArrayDestroy(pInfo->pTombData);
    }
  }
}

static int32_t nextRowIterOpen(CacheNextRowIter *pIter, tb_uid_t uid, STsdb *pTsdb, STSchema *pTSchema, tb_uid_t suid,
                               SArray *pLDataIterArray, STsdbReadSnap *pReadSnap, int64_t lastTs,
                               SCacheRowsReader *pr) {
  int code = 0;

  STbData *pMem = NULL;
  if (pReadSnap->pMem) {
    pMem = tsdbGetTbDataFromMemTable(pReadSnap->pMem, suid, uid);
  }

  STbData *pIMem = NULL;
  if (pReadSnap->pIMem) {
    pIMem = tsdbGetTbDataFromMemTable(pReadSnap->pIMem, suid, uid);
  }

  pIter->pTsdb = pTsdb;

  pIter->pMemDelData = NULL;

  loadMemTombData(&pIter->pMemDelData, pMem, pIMem, pr->info.verRange.maxVer);

  pIter->idx = (SBlockIdx){.suid = suid, .uid = uid};

  pIter->fsState.pRowIter = pIter;
  pIter->fsState.state = SFSNEXTROW_FS;
  pIter->fsState.pTsdb = pTsdb;
  pIter->fsState.aDFileSet = pReadSnap->pfSetArray;
  pIter->fsState.pBlockIdxExp = &pIter->idx;
  pIter->fsState.pTSchema = pTSchema;
  pIter->fsState.suid = suid;
  pIter->fsState.uid = uid;
  pIter->fsState.lastTs = lastTs;
  pIter->fsState.pr = pr;

  pIter->input[0] = (TsdbNextRowState){&pIter->memRow, true, false, false, &pIter->memState, getNextRowFromMem, NULL};
  pIter->input[1] = (TsdbNextRowState){&pIter->imemRow, true, false, false, &pIter->imemState, getNextRowFromMem, NULL};
  pIter->input[2] =
      (TsdbNextRowState){&pIter->fsRow, false, true, false, &pIter->fsState, getNextRowFromFS, clearNextRowFromFS};

  if (pMem) {
    pIter->memState.pMem = pMem;
    pIter->memState.state = SMEMNEXTROW_ENTER;
    pIter->memState.lastTs = lastTs;
    pIter->input[0].stop = false;
    pIter->input[0].next = true;
  }

  if (pIMem) {
    pIter->imemState.pMem = pIMem;
    pIter->imemState.state = SMEMNEXTROW_ENTER;
    pIter->imemState.lastTs = lastTs;
    pIter->input[1].stop = false;
    pIter->input[1].next = true;
  }

  pIter->pr = pr;
_err:
  return code;
}

static int32_t nextRowIterClose(CacheNextRowIter *pIter) {
  int code = 0;

  for (int i = 0; i < 3; ++i) {
    if (pIter->input[i].nextRowClearFn) {
      pIter->input[i].nextRowClearFn(pIter->input[i].iter);
    }
  }

  if (pIter->pSkyline) {
    taosArrayDestroy(pIter->pSkyline);
  }

  if (pIter->pMemDelData) {
    taosArrayDestroy(pIter->pMemDelData);
  }

_err:
  return code;
}

// iterate next row non deleted backward ts, version (from high to low)
static int32_t nextRowIterGet(CacheNextRowIter *pIter, TSDBROW **ppRow, bool *pIgnoreEarlierTs, bool isLast,
                              int16_t *aCols, int nCols) {
  int code = 0;
  for (;;) {
    for (int i = 0; i < 3; ++i) {
      if (pIter->input[i].next && !pIter->input[i].stop) {
        code = pIter->input[i].nextRowFn(pIter->input[i].iter, &pIter->input[i].pRow, &pIter->input[i].ignoreEarlierTs,
                                         isLast, aCols, nCols);
        if (code) goto _err;

        if (pIter->input[i].pRow == NULL) {
          pIter->input[i].stop = true;
          pIter->input[i].next = false;
        }
      }
    }

    if (pIter->input[0].stop && pIter->input[1].stop && pIter->input[2].stop) {
      *ppRow = NULL;
      *pIgnoreEarlierTs =
          (pIter->input[0].ignoreEarlierTs || pIter->input[1].ignoreEarlierTs || pIter->input[2].ignoreEarlierTs);
      return code;
    }

    // select maxpoint(s) from mem, imem, fs and last
    TSDBROW *max[4] = {0};
    int      iMax[4] = {-1, -1, -1, -1};
    int      nMax = 0;
    TSKEY    maxKey = TSKEY_MIN;

    for (int i = 0; i < 3; ++i) {
      if (!pIter->input[i].stop && pIter->input[i].pRow != NULL) {
        TSDBKEY key = TSDBROW_KEY(pIter->input[i].pRow);

        // merging & deduplicating on client side
        if (maxKey <= key.ts) {
          if (maxKey < key.ts) {
            nMax = 0;
            maxKey = key.ts;
          }

          iMax[nMax] = i;
          max[nMax++] = pIter->input[i].pRow;
        } else {
          pIter->input[i].next = false;
        }
      }
    }

    // delete detection
    TSDBROW *merge[4] = {0};
    int      iMerge[4] = {-1, -1, -1, -1};
    int      nMerge = 0;
    for (int i = 0; i < nMax; ++i) {
      TSDBKEY maxKey1 = TSDBROW_KEY(max[i]);

      if (!pIter->pSkyline) {
        pIter->pSkyline = taosArrayInit(32, sizeof(TSDBKEY));

        uint64_t        uid = pIter->idx.uid;
        STableLoadInfo *pInfo = *(STableLoadInfo **)tSimpleHashGet(pIter->pr->pTableMap, &uid, sizeof(uid));
        SArray         *pTombData = pInfo->pTombData;
        if (pTombData) {
          taosArrayAddAll(pTombData, pIter->pMemDelData);

          code = tsdbBuildDeleteSkyline(pTombData, 0, (int32_t)(TARRAY_SIZE(pTombData) - 1), pIter->pSkyline);
        }

        pIter->iSkyline = taosArrayGetSize(pIter->pSkyline) - 1;
      }

      bool deleted = tsdbKeyDeleted(&maxKey1, pIter->pSkyline, &pIter->iSkyline);
      if (!deleted) {
        iMerge[nMerge] = iMax[i];
        merge[nMerge++] = max[i];
      }

      pIter->input[iMax[i]].next = deleted;
    }

    if (nMerge > 0) {
      pIter->input[iMerge[0]].next = true;

      *ppRow = merge[0];
      return code;
    }
  }

_err:
  return code;
}

