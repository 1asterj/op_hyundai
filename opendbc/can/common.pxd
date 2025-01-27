# distutils: language = c++
# cython: language_level=3

from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libcpp cimport bool
from libcpp.map cimport map
from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.unordered_set cimport unordered_set


cdef extern from "common_dbc.h":
  ctypedef enum SignalType:
    DEFAULT,
    HONDA_CHECKSUM,
    HONDA_COUNTER,
    TOYOTA_CHECKSUM,
    PEDAL_CHECKSUM,
    PEDAL_COUNTER,
    VOLKSWAGEN_CHECKSUM,
    VOLKSWAGEN_COUNTER,
    SUBARU_CHECKSUM,
    CHRYSLER_CHECKSUM

  cdef struct Signal:
    const char* name
    int start_bit, msb, lsb, size
    bool is_signed
    double factor, offset
    SignalType type

  cdef struct Msg:
    const char* name
    uint32_t address
    unsigned int size
    size_t num_sigs
    const Signal *sigs

  cdef struct Val:
    const char* name
    uint32_t address
    const char* def_val
    const Signal *sigs

  cdef struct DBC:
    const char* name
    size_t num_msgs
    const Msg *msgs
    const Val *vals
    size_t num_vals

  cdef struct SignalParseOptions:
    uint32_t address
    const char* name


  cdef struct MessageParseOptions:
    uint32_t address
    int check_frequency

  cdef struct SignalValue:
    uint32_t address
    const char* name
    double value
    vector[double] all_values

  cdef struct SignalPackValue:
    string name
    double value


cdef extern from "common.h":
  cdef const DBC* dbc_lookup(const string);

  cdef cppclass CANParser:
    bool can_valid
    bool bus_timeout
    CANParser(int, string, vector[MessageParseOptions], vector[SignalParseOptions])
    void update_string(string, bool)
    vector[SignalValue] query_latest()

  cdef cppclass CANPacker:
   CANPacker(string)
   vector[uint8_t] pack(uint32_t, vector[SignalPackValue], int counter)