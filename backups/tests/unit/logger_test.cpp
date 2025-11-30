#include "observability/logger.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace protogate::observability;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::initialize(LogLevel::DEBUG);
    }
};

TEST_F(LoggerTest, BasicLogging) {
    // Test that logger can be called without throwing
    EXPECT_NO_THROW({
        LOG_INFO("Test message");
        LOG_DEBUG("Debug message");
        LOG_WARNING("Warning message");
        LOG_ERROR("Error message");
    });
}

TEST_F(LoggerTest, StructuredFields) {
    EXPECT_NO_THROW({
        Logger::instance().info("User action", {
            {"user_id", "user123"},
            {"action", "login"},
            {"ip", "192.168.1.1"}
        });
    });
}

TEST_F(LoggerTest, LogLevelFiltering) {
    Logger::instance().set_min_level(LogLevel::WARNING);
    
    // These should be filtered out (no assertion, just ensure no crash)
    LOG_DEBUG("Should not appear");
    LOG_INFO("Should not appear");
    
    // These should appear
    LOG_WARNING("Should appear");
    LOG_ERROR("Should appear");
}

TEST_F(LoggerTest, FlushDoesNotCrash) {
    LOG_INFO("Test message");
    EXPECT_NO_THROW({
        Logger::instance().flush();
    });
}
