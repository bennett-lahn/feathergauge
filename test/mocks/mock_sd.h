#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include "mock_registry.h"

// Open-mode flags matching SdFat so firmware compiles unchanged on native builds.

#define O_READ    0x01
#define O_WRITE   0x02
#define O_CREAT   0x10
#define O_APPEND  0x20
#define O_AT_END  0x40
#define O_TRUNC   0x08

// Timestamp flags for File32::timestamp() (stubbed on native; real SdFat sets directory entry times).
#define T_ACCESS 0x01
#define T_CREATE 0x02
#define T_WRITE  0x04

// Stand-in for SdFat File32: CSV data accumulates in mockReg.state.fileData instead of a real SD card.
// Implements the subset of File32 methods used in writeToOutputFile(), setup(), and recoverSdCard() call.
class File32 {
public:
    bool _open = false;
    bool _writeError = false;

    // Result comes from mockReg.input.fileOpenResults queue; updates mockReg.state.fileIsOpen.
    // setup() and recoverSdCard() both call open(); queue entries in call order for multi-step tests.
    bool open(const char*, uint8_t) {
        bool result = mockReg.popFileOpen();
        _open = result;
        mockReg.state.fileIsOpen = result;
        return result;
    }

    explicit operator bool() const { return _open; }

    // Marks the handle closed and increments mockReg.audit.closeCount for recovery/rollover tests.
    // Rollover and recoverSdCard() both close the file; the counter tracks how many times.
    void close() {
        _open = false;
        mockReg.state.fileIsOpen = false;
        mockReg.audit.closeCount++;
    }

    // Result from mockReg.input.syncResults queue; increments mockReg.audit.syncCount on each call.
    // loop() syncs when secondsSinceFlush reaches FLUSH_INTERVAL_SECONDS; audit.syncCount verifies that.
    bool sync() {
        mockReg.audit.syncCount++;
        return mockReg.popSync();
    }

    // Appends raw bytes to the in-memory CSV buffer (same path as writeToOutputFile row data).
    // Tests inspect mockReg.state.fileData to assert CSV content without a real filesystem.
    size_t write(const uint8_t* buf, size_t n) {
        mockReg.state.fileData.append(reinterpret_cast<const char*>(buf), n);
        return n;
    }

    // Single-byte write; used by firmware for newline and small literal output.
    size_t write(uint8_t b) {
        mockReg.state.fileData.push_back((char)b);
        return 1;
    }

    // Formats integers via snprintf; char* must use the const char* overload below instead.
    // Template covers int/uint fields in CSV rows; pointers would format as integers without the overload.
    template<typename T>
    size_t print(T val) {
        char buf[64];
        if constexpr (sizeof(T) <= 4) {
            int written = snprintf(buf, sizeof(buf), "%d", (int)val);
            mockReg.state.fileData.append(buf, (size_t)written);
            return (size_t)written;
        }
        return 0;
    }

    // Preferred path for string literals and serial numbers (template would treat pointers as integers).
    // Header row and filename strings in writeToOutputFile() rely on this overload.
    size_t print(const char* s) {
        size_t n = strlen(s);
        mockReg.state.fileData.append(s, n);
        return n;
    }

    // Appends CRLF after the string; matches SdFat println() line endings in CSV output.
    size_t println(const char* s = "") {
        size_t n = print(s);
        mockReg.state.fileData.append("\r\n", 2);
        return n + 2;
    }

    template<typename T>
    size_t println(T val) {
        size_t n = print(val);
        mockReg.state.fileData.append("\r\n", 2);
        return n + 2;
    }

    // Returns mockReg.input.fileSizeOverride when set (for rollover tests), else buffer length.
    // writeToOutputFile() compares size() + row length against FILE_ROLLOVER_SIZE_BYTES.
    uint32_t size() const {
        if (mockReg.input.useFileSizeOverride) return mockReg.input.fileSizeOverride;
        return (uint32_t)mockReg.state.fileData.size();
    }

    // Result from mockReg.input.writeErrorResults queue; can trigger recoverSdCard() when true.
    // Called after each write batch; true simulates a sticky SD controller error flag.
    bool getWriteError() {
        _writeError = mockReg.popWriteError();
        return _writeError;
    }

    // Clears the local error flag after recoverSdCard() succeeds.
    void clearWriteError() { _writeError = false; }

    // No-op: filesystem metadata timestamps are not modeled; firmware calls this after opening a new file.
    // Real SdFat sets FAT directory entry times; native tests only care about CSV byte content.
    void timestamp(uint8_t, uint16_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) {}
};

// Stand-in for SdFat32: mount and filename-exists checks without SPI or a physical card.
// begin() and exists() gate setup(), recoverSdCard(), and makeFileName() iteration logic.
class SdFat32 {
public:
    // Always returns mockReg.input.sdBeginResult (tests set false to simulate mount failure).
    // setup() and recoverSdCard() both call begin(); false triggers error() or retry logic.
    bool begin(uint8_t, uint8_t = 0) {
        return mockReg.input.sdBeginResult;
    }

    // Result from mockReg.input.existsResults queue; makeFileName() walks indices until false.
    // Push true for each _IT-XX slot already occupied on the simulated card.
    bool exists(const char*) {
        return mockReg.popExists();
    }
};
