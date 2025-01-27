#pragma once

#include <vector>
#include <map>
#include <unordered_map>

#include "common_dbc.h"
#include <capnp/dynamic.h>
#include <capnp/serialize.h>

#ifndef DYNAMIC_CAPNP
#include "cereal/gen/cpp/log.capnp.h"
#endif

#define INFO printf
#define WARN printf
#define DEBUG(...)
//#define DEBUG printf

#define MAX_BAD_COUNTER 5

// Car specific functions
unsigned int honda_checksum(uint32_t address, const std::vector<uint8_t> &d);
unsigned int toyota_checksum(uint32_t address, const std::vector<uint8_t> &d);
unsigned int subaru_checksum(uint32_t address, const std::vector<uint8_t> &d);
unsigned int chrysler_checksum(uint32_t address, const std::vector<uint8_t> &d);
void init_crc_lookup_tables();
unsigned int volkswagen_crc(uint32_t address, const std::vector<uint8_t> &d);
unsigned int pedal_checksum(const std::vector<uint8_t> &d);

class MessageState {
public:
  uint32_t address;
  unsigned int size;

  std::vector<Signal> parse_sigs;
  std::vector<double> vals;
  std::vector<std::vector<double>> all_vals;

  uint64_t seen;
  uint64_t check_threshold;

  uint8_t counter;
  uint8_t counter_fail;

  bool ignore_checksum = false;
  bool ignore_counter = false;

  bool parse(uint64_t sec, const std::vector<uint8_t> &dat);
  bool update_counter_generic(int64_t v, int cnt_size);
};

class CANParser {
private:
  const int bus;
  kj::Array<capnp::word> aligned_buf;

  const DBC *dbc = NULL;
  std::unordered_map<uint32_t, MessageState> message_states;

public:
  bool can_valid = false;
  bool bus_timeout = false;
  uint64_t last_sec = 0;
  uint64_t last_nonempty_sec = 0;
  uint64_t bus_timeout_threshold = 0;

  CANParser(int abus, const std::string& dbc_name,
            const std::vector<MessageParseOptions> &options,
            const std::vector<SignalParseOptions> &sigoptions);
  CANParser(int abus, const std::string& dbc_name, bool ignore_checksum, bool ignore_counter);
  #ifndef DYNAMIC_CAPNP
  void update_string(const std::string &data, bool sendcan);
  void UpdateCans(uint64_t sec, const capnp::List<cereal::CanData>::Reader& cans);
  #endif
  void UpdateCans(uint64_t sec, const capnp::DynamicStruct::Reader& cans);
  void UpdateValid(uint64_t sec);
  std::vector<SignalValue> query_latest();
};

class CANPacker {
private:
  const DBC *dbc = NULL;
  std::map<std::pair<uint32_t, std::string>, Signal> signal_lookup;
  std::map<uint32_t, Msg> message_lookup;

public:
  CANPacker(const std::string& dbc_name);
  std::vector<uint8_t> pack(uint32_t address, const std::vector<SignalPackValue> &values, int counter);
  Msg* lookup_message(uint32_t address);
};