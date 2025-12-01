/*
 * Test whether OCIDefineDynamic reduces network round-trips for LOB queries
 * Compile: gcc -I$ORACLE_HOME/rdbms/public -L$ORACLE_HOME/lib -lclntsh test_dynamic_fetch_roundtrips.c -o test_dynamic
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <oci.h>
#include <time.h>

static OCIEnv *g_envhp;
static OCIError *g_errhp;
static int callback_count = 0;

void check_error(sword status, const char *msg)
{
    text errbuf[512];
    sb4 errcode = 0;

    if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
        OCIErrorGet((dvoid *)g_errhp, (ub4)1, (text *)NULL, &errcode,
                    errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
        printf("ERROR: %s - %.*s\n", msg, 512, errbuf);
        exit(1);
    }
}

/* Callback for NUMBER column */
static sb4 number_callback(void *octxp, OCIDefine *defnp, ub4 iter,
                          void **bufpp, ub4 **alenp, ub1 *piecep,
                          void **indpp, ub2 **rcodep)
{
    static int numbers[100];
    static ub4 lengths[100];
    static sb2 indicators[100];

    callback_count++;

    *bufpp = &numbers[iter];
    *alenp = &lengths[iter];
    *indpp = &indicators[iter];
    *piecep = OCI_ONE_PIECE;

    lengths[iter] = sizeof(int);

    return OCI_CONTINUE;
}

/* Callback for VARCHAR2 column */
static sb4 varchar_callback(void *octxp, OCIDefine *defnp, ub4 iter,
                            void **bufpp, ub4 **alenp, ub1 *piecep,
                            void **indpp, ub2 **rcodep)
{
    static char buffer[100][200];
    static ub4 lengths[100];
    static sb2 indicators[100];

    callback_count++;

    *bufpp = buffer[iter];
    *alenp = &lengths[iter];
    *indpp = &indicators[iter];
    *piecep = OCI_ONE_PIECE;

    lengths[iter] = sizeof(buffer[iter]);

    return OCI_CONTINUE;
}

/* Callback for LOB locator - THIS IS THE KEY TEST */
static sb4 lob_callback(void *octxp, OCIDefine *defnp, ub4 iter,
                       void **bufpp, ub4 **alenp, ub1 *piecep,
                       void **indpp, ub2 **rcodep)
{
    static OCILobLocator *lob_locs[100];
    static ub4 lengths[100];
    static sb2 indicators[100];
    sword status;

    callback_count++;

    printf("  [LOB callback iter=%u]\n", iter);

    /* Allocate LOB descriptor if needed */
    if (lob_locs[iter] == NULL) {
        status = OCIDescriptorAlloc((dvoid *)g_envhp, (dvoid **)&lob_locs[iter],
                                   OCI_DTYPE_LOB, 0, NULL);
        if (status != OCI_SUCCESS) {
            printf("    ERROR: OCIDescriptorAlloc failed\n");
            return OCI_ERROR;
        }
        printf("    Allocated LOB descriptor for iter %u\n", iter);
    }

    /* Provide LOB locator pointer */
    *bufpp = lob_locs[iter];
    *alenp = &lengths[iter];
    *indpp = &indicators[iter];
    *piecep = OCI_ONE_PIECE;

    lengths[iter] = 0;  /* Let Oracle fill this in */

    return OCI_CONTINUE;
}

int main(int argc, char *argv[])
{
    OCIServer *srvhp;
    OCISvcCtx *svchp;
    OCISession *usrhp;
    OCIStmt *stmthp;
    OCIDefine *defnp_id, *defnp_desc, *defnp_lob;
    sword status;

    char *username = "ruby";
    char *password = "ruby";
    char *dbname = "127.0.0.1:1521/systempdb";

    if (argc >= 4) {
        username = argv[1];
        password = argv[2];
        dbname = argv[3];
    }

    printf("=== Testing OCIDefineDynamic with LOB columns ===\n\n");

    /* Initialize OCI */
    printf("Initializing OCI...\n");
    status = OCIInitialize(OCI_DEFAULT, NULL, NULL, NULL, NULL);
    check_error(status, "OCIInitialize");

    status = OCIEnvInit(&g_envhp, OCI_DEFAULT, 0, NULL);
    check_error(status, "OCIEnvInit");

    status = OCIHandleAlloc(g_envhp, (dvoid **)&g_errhp, OCI_HTYPE_ERROR, 0, NULL);
    check_error(status, "OCIHandleAlloc ERROR");

    status = OCIHandleAlloc(g_envhp, (dvoid **)&srvhp, OCI_HTYPE_SERVER, 0, NULL);
    check_error(status, "OCIHandleAlloc SERVER");

    status = OCIHandleAlloc(g_envhp, (dvoid **)&svchp, OCI_HTYPE_SVCCTX, 0, NULL);
    check_error(status, "OCIHandleAlloc SVCCTX");

    status = OCIHandleAlloc(g_envhp, (dvoid **)&usrhp, OCI_HTYPE_SESSION, 0, NULL);
    check_error(status, "OCIHandleAlloc SESSION");

    /* Connect */
    printf("Connecting to %s as %s...\n", dbname, username);
    status = OCIServerAttach(srvhp, g_errhp, (text *)dbname, strlen(dbname), OCI_DEFAULT);
    check_error(status, "OCIServerAttach");

    status = OCIAttrSet(svchp, OCI_HTYPE_SVCCTX, srvhp, 0, OCI_ATTR_SERVER, g_errhp);
    check_error(status, "OCIAttrSet SERVER");

    status = OCIAttrSet(usrhp, OCI_HTYPE_SESSION, username, strlen(username), OCI_ATTR_USERNAME, g_errhp);
    check_error(status, "OCIAttrSet USERNAME");

    status = OCIAttrSet(usrhp, OCI_HTYPE_SESSION, password, strlen(password), OCI_ATTR_PASSWORD, g_errhp);
    check_error(status, "OCIAttrSet PASSWORD");

    status = OCISessionBegin(svchp, g_errhp, usrhp, OCI_CRED_RDBMS, OCI_DEFAULT);
    check_error(status, "OCISessionBegin");

    status = OCIAttrSet(svchp, OCI_HTYPE_SVCCTX, usrhp, 0, OCI_ATTR_SESSION, g_errhp);
    check_error(status, "OCIAttrSet SESSION");

    /* Prepare statement with LOB column */
    printf("\nPreparing statement...\n");
    status = OCIHandleAlloc(g_envhp, (dvoid **)&stmthp, OCI_HTYPE_STMT, 0, NULL);
    check_error(status, "OCIHandleAlloc STMT");

    char *sql = "SELECT id, description, content FROM test_lob_fetch WHERE ROWNUM <= 100 ORDER BY id";
    printf("SQL: %s\n", sql);
    status = OCIStmtPrepare(stmthp, g_errhp, (text *)sql, strlen(sql), OCI_NTV_SYNTAX, OCI_DEFAULT);
    check_error(status, "OCIStmtPrepare");

    /* Define columns with DYNAMIC FETCH */
    printf("\nDefining columns with OCI_DYNAMIC_FETCH...\n");

    // Column 1: id (NUMBER) - dynamic
    printf("  Column 1: id (NUMBER)\n");
    status = OCIDefineByPos(stmthp, &defnp_id, g_errhp, 1, NULL, sizeof(int),
                           SQLT_INT, NULL, NULL, NULL, OCI_DYNAMIC_FETCH);
    check_error(status, "OCIDefineByPos id");

    status = OCIDefineDynamic(defnp_id, g_errhp, NULL, number_callback);
    check_error(status, "OCIDefineDynamic id");

    // Column 2: description (VARCHAR2) - dynamic
    printf("  Column 2: description (VARCHAR2)\n");
    status = OCIDefineByPos(stmthp, &defnp_desc, g_errhp, 2, NULL, 200,
                           SQLT_CHR, NULL, NULL, NULL, OCI_DYNAMIC_FETCH);
    check_error(status, "OCIDefineByPos description");

    status = OCIDefineDynamic(defnp_desc, g_errhp, NULL, varchar_callback);
    check_error(status, "OCIDefineDynamic description");

    // Column 3: content (CLOB) - dynamic, LOB locator
    printf("  Column 3: content (CLOB) - THE CRITICAL TEST\n");
    status = OCIDefineByPos(stmthp, &defnp_lob, g_errhp, 3, NULL, 0,
                           SQLT_CLOB, NULL, NULL, NULL, OCI_DYNAMIC_FETCH);
    check_error(status, "OCIDefineByPos CLOB");

    status = OCIDefineDynamic(defnp_lob, g_errhp, g_envhp, lob_callback);
    check_error(status, "OCIDefineDynamic CLOB");

    /* Execute */
    printf("\nExecuting statement...\n");
    status = OCIStmtExecute(svchp, stmthp, g_errhp, 0, 0, NULL, NULL, OCI_DEFAULT);
    check_error(status, "OCIStmtExecute");

    /* CRITICAL TEST: Fetch 100 rows at once */
    printf("\n=== CRITICAL TEST ===\n");
    printf("Calling OCIStmtFetch(nrows=100)...\n");

    clock_t start = clock();
    callback_count = 0;

    status = OCIStmtFetch(stmthp, g_errhp, 100, OCI_FETCH_NEXT, OCI_DEFAULT);

    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;

    if (status != OCI_SUCCESS && status != OCI_NO_DATA) {
        check_error(status, "OCIStmtFetch");
    }

    /* Get actual rows fetched */
    ub4 rows_fetched = 0;
    OCIAttrGet(stmthp, OCI_HTYPE_STMT, &rows_fetched, NULL, OCI_ATTR_ROWS_FETCHED, g_errhp);

    printf("\n=== RESULTS ===\n");
    printf("OCIStmtFetch returned: %d (%s)\n", status,
           status == OCI_SUCCESS ? "SUCCESS" :
           status == OCI_NO_DATA ? "NO_DATA" : "OTHER");
    printf("Rows fetched: %u\n", rows_fetched);
    printf("Total callback invocations: %d\n", callback_count);
    printf("Expected callbacks: %u (3 columns × %u rows)\n", rows_fetched * 3, rows_fetched);
    printf("Elapsed time: %.6f seconds\n", elapsed);

    /* Analysis */
    printf("\n=== ANALYSIS ===\n");
    int expected_callbacks = rows_fetched * 3;  /* 3 columns */

    if (callback_count == expected_callbacks) {
        printf("✓ Callbacks invoked %d times (correct: %u rows × 3 columns)\n",
               callback_count, rows_fetched);
        if (elapsed < 0.05) {
            printf("✓ Very fast execution (%.6fs) = ONE ROUND TRIP!\n", elapsed);
            printf("\n** CONCLUSION: OCIDefineDynamic DOES work with LOBs! **\n");
            printf("** Piecewise fetch is VIABLE - proceed with implementation! **\n");
        } else if (elapsed < 0.2) {
            printf("? Moderate execution time (%.6fs)\n", elapsed);
            printf("   Suggests possible batching, run with tcpdump to verify\n");
        } else {
            printf("✗ Slow execution (%.6fs) = MULTIPLE ROUND TRIPS\n", elapsed);
            printf("\n** CONCLUSION: OCIDefineDynamic does NOT reduce round trips **\n");
            printf("** Piecewise fetch is NOT viable - use alternative solutions **\n");
        }
    } else {
        printf("✗ Unexpected callback count: %d (expected %d)\n",
               callback_count, expected_callbacks);
        printf("   Investigation needed\n");
    }

    /* Cleanup */
    printf("\nCleaning up...\n");
    OCISessionEnd(svchp, g_errhp, usrhp, OCI_DEFAULT);
    OCIServerDetach(srvhp, g_errhp, OCI_DEFAULT);
    OCIHandleFree(stmthp, OCI_HTYPE_STMT);
    OCIHandleFree(usrhp, OCI_HTYPE_SESSION);
    OCIHandleFree(svchp, OCI_HTYPE_SVCCTX);
    OCIHandleFree(srvhp, OCI_HTYPE_SERVER);
    OCIHandleFree(g_errhp, OCI_HTYPE_ERROR);
    OCIHandleFree(g_envhp, OCI_HTYPE_ENV);

    printf("Done.\n");
    return 0;
}
