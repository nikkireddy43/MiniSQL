#include <gtest/gtest.h>
#include <cstdio>
#include "storage/Value.h"
#include "wal/WriteAheadLog.h"

using namespace minisql;

class WalTest : public ::testing::Test {
protected:
    std::string logFile = "test_minisql_wal.log";
    std::string dataFile = "test_minisql_wal_data.db";

    void TearDown() override {
        std::remove(logFile.c_str());
        std::remove(dataFile.c_str());
    }
};

TEST_F(WalTest, RecoverWithNoLogFileDoesNothing) {
    DiskManager disk(dataFile);
    EXPECT_NO_THROW(WriteAheadLog::recover(logFile, disk));
    EXPECT_EQ(disk.numPages(), 0);
}

TEST_F(WalTest, RecoverWithEmptyLogDoesNothing) {
    { WriteAheadLog wal(logFile); }  // creates an empty log file, then closes
    DiskManager disk(dataFile);
    EXPECT_NO_THROW(WriteAheadLog::recover(logFile, disk));
    EXPECT_EQ(disk.numPages(), 0);
}

// THE CORE DEMONSTRATION: log a write WITHOUT ever calling
// diskManager.writePage() for it directly - simulating a crash that
// happened after the log entry was durably written but before the real
// page write ever occurred. Confirm the data file is missing it BEFORE
// recovery, and has it correctly AFTER recovery replays the log.
TEST_F(WalTest, SimulatedCrashRecoveredByReplayingLog) {
    {
        DiskManager disk(dataFile);
        disk.allocatePage();  // page 0 exists on disk, but stays empty

        WriteAheadLog wal(logFile);
        Page page;
        page.appendRecord({Value::makeInt(42), Value::makeText("Nikki")});
        wal.logPageWrite(0, page);
        // Deliberately NOT calling disk.writePage(0, page) here - this
        // models the write existing only in the log, as if the process
        // crashed right after the log append but before the real page
        // write completed.
    }

    // "Before recovery": the data file should NOT yet reflect the write.
    {
        DiskManager disk(dataFile);
        Page page;
        disk.readPage(0, page);
        EXPECT_EQ(page.numRecords(), 0) << "Data file shouldn't have the write yet - only the log does";
    }

    // Recovery: replay the log onto the data file.
    {
        DiskManager disk(dataFile);
        WriteAheadLog::recover(logFile, disk);
    }

    // "After recovery": the data file now has it.
    {
        DiskManager disk(dataFile);
        Page page;
        disk.readPage(0, page);
        auto records = page.getAllRecords();
        ASSERT_EQ(records.size(), 1);
        EXPECT_EQ(records[0][0].intVal, 42);
        EXPECT_EQ(records[0][1].textVal, "Nikki");
    }
}

TEST_F(WalTest, MultipleWritesToSamePageRecoversToLastOne) {
    {
        DiskManager disk(dataFile);
        disk.allocatePage();
        WriteAheadLog wal(logFile);

        Page firstVersion;
        firstVersion.appendRecord({Value::makeInt(1)});
        wal.logPageWrite(0, firstVersion);

        Page secondVersion;
        secondVersion.appendRecord({Value::makeInt(2)});
        secondVersion.appendRecord({Value::makeInt(3)});
        wal.logPageWrite(0, secondVersion);  // supersedes the first log entry
    }

    DiskManager disk(dataFile);
    WriteAheadLog::recover(logFile, disk);

    Page page;
    disk.readPage(0, page);
    auto records = page.getAllRecords();
    ASSERT_EQ(records.size(), 2) << "Should reflect the SECOND logged write, not the first";
    EXPECT_EQ(records[0][0].intVal, 2);
    EXPECT_EQ(records[1][0].intVal, 3);
}

TEST_F(WalTest, MultiplePagesAllRecoverCorrectly) {
    {
        DiskManager disk(dataFile);
        disk.allocatePage();
        disk.allocatePage();
        disk.allocatePage();
        WriteAheadLog wal(logFile);

        for (int32_t i = 0; i < 3; i++) {
            Page page;
            page.appendRecord({Value::makeInt(i * 100)});
            wal.logPageWrite(i, page);
        }
    }

    DiskManager disk(dataFile);
    WriteAheadLog::recover(logFile, disk);

    for (int32_t i = 0; i < 3; i++) {
        Page page;
        disk.readPage(i, page);
        auto records = page.getAllRecords();
        ASSERT_EQ(records.size(), 1);
        EXPECT_EQ(records[0][0].intVal, i * 100);
    }
}

TEST_F(WalTest, ClearThenRecoverDoesNothing) {
    {
        DiskManager disk(dataFile);
        disk.allocatePage();
        WriteAheadLog wal(logFile);
        Page page;
        page.appendRecord({Value::makeInt(7)});
        wal.logPageWrite(0, page);
        wal.clear();  // the logged write is discarded before recovery ever sees it
    }

    DiskManager disk(dataFile);
    WriteAheadLog::recover(logFile, disk);

    Page page;
    disk.readPage(0, page);
    EXPECT_EQ(page.numRecords(), 0) << "Log was cleared - nothing should have been replayed";
}
