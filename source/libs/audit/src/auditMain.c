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

#define _DEFAULT_SOURCE
#include "auditInt.h"
#include "taoserror.h"
#include "thttp.h"
#include "ttime.h"
#include "tjson.h"
#include "tglobal.h"

SAudit tsAudit = {0};
char* tsAuditUri = "/audit";

int32_t auditInit(const SAuditCfg *pCfg) {
  tsAudit.cfg = *pCfg;
  return 0;
}

extern void auditRecordImp(SRpcMsg *pReq, char *oper, char *db, char *stable, int32_t detailLen, char *detail);

void auditRecord(SRpcMsg *pReq, char *oper, char *db, char *stable, int32_t detailLen, char *detail) {
  auditRecordImp(pReq, oper, db, stable, detailLen, detail);
}

#ifndef TD_ENTERPRISE
void auditRecordImp(SRpcMsg *pReq, char *oper, char *db, char *stable, int32_t detailLen, char *detail) {
}
#endif

