#define BOOST_TEST_MODULE trace_trace_file
#include <boost/test/included/unit_test.hpp>
#include <fc/io/cfile.hpp>
#include <eosio/trace_api_plugin/test_common.hpp>
#include <eosio/trace_api_plugin/store_provider.hpp>
#include <boost/filesystem.hpp>

using namespace eosio;
using namespace eosio::trace_api_plugin;
using namespace eosio::trace_api_plugin::test_common;
namespace bfs = boost::filesystem;

namespace {
   struct test_fixture {

      const block_trace_v0 bt {
         "0000000000000000000000000000000000000000000000000000000000000001"_h,
         1,
         "0000000000000000000000000000000000000000000000000000000000000003"_h,
         chain::block_timestamp_type(1),
         "bp.one"_n,
         {
            {
               "0000000000000000000000000000000000000000000000000000000000000001"_h,
               chain::transaction_receipt_header::hard_fail,
               {
                  {
                     0,
                     "eosio.token"_n, "eosio.token"_n, "transfer"_n,
                     {{ "alice"_n, "active"_n }},
                     make_transfer_data( "alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" )
                  },
                  {
                     1,
                     "alice"_n, "eosio.token"_n, "transfer"_n,
                     {{ "alice"_n, "active"_n }},
                     make_transfer_data( "alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" )
                  },
                  {
                     2,
                     "bob"_n, "eosio.token"_n, "transfer"_n,
                     {{ "alice"_n, "active"_n }},
                     make_transfer_data( "alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" )
                  }
               }
            }
         }
      };

      const block_trace_v0 bt2 {
         "0000000000000000000000000000000000000000000000000000000000000002"_h,
         1,
         "0000000000000000000000000000000000000000000000000000000000000005"_h,
         chain::block_timestamp_type(2),
         "bp.two"_n,
         {
            {
               "f000000000000000000000000000000000000000000000000000000000000004"_h,
               chain::transaction_receipt_header::soft_fail,
               {}
            }
         }
      };

      const metadata_log_entry be1 { block_entry_v0 {
         "b000000000000000000000000000000000000000000000000000000000000001"_h, 5, 0
      } };
      const metadata_log_entry le1 { lib_entry_v0 { 4 } };
      const metadata_log_entry be2 { block_entry_v0 {
         "b000000000000000000000000000000000000000000000000000000000000002"_h, 7, 0
      } };
      const metadata_log_entry le2 { lib_entry_v0 { 5 } };

   };

   class vslice_datastream;

   struct vslice {
      enum mode { read_mode, write_mode};
      vslice(mode m = write_mode) : _mode(m) {}
      long tellp() const {
         return _pos;
      }

      void seek( long loc ) {
         if (_mode == read_mode) {
            if (loc > _buffer.size()) {
               throw std::ios_base::failure( "read vslice unable to seek to: " + std::to_string(loc) + ", end is at: " + std::to_string(_buffer.size()));
            }
         }
         _pos = loc;
      }

      void seek_end( long loc ) {
         _pos = _buffer.size();
      }

      void read( char* d, size_t n ) {
         if( _pos + n > _buffer.size() ) {
            throw std::ios_base::failure( "vslice unable to read " + std::to_string( n ) + " bytes; only can read " + std::to_string( _buffer.size() - _pos ) );
         }
         std::memcpy( d, _buffer.data() + _pos, n);
         _pos += n;
      }

      void write( const char* d, size_t n ) {
         if (_mode == read_mode) {
            throw std::ios_base::failure( "read vslice should not have write called" );
         }
         if (_buffer.size() < _pos + n) {
            _buffer.resize(_pos + n);
         }
         std::memcpy( _buffer.data() + _pos, d, n);
         _pos += n;
      }

      void flush() {
         _flush = true;
      }

      void sync() {
         _sync = true;
      }

      vslice_datastream create_datastream();

      std::vector<char> _buffer;
      mode _mode = write_mode;
      long _pos = 0l;
      bool _flush = false;
      bool _sync = false;
   };

   class vslice_datastream {
   public:
      explicit vslice_datastream( vslice& vs ) : _vs(vs) {}

      void skip( size_t s ) {
         std::vector<char> d( s );
         read( &d[0], s );
      }

      bool read( char* d, size_t s ) {
         _vs.read( d, s );
         return true;
      }

      bool get( unsigned char& c ) { return get( *(char*)&c ); }

      bool get( char& c ) { return read(&c, 1); }

   private:
      vslice& _vs;
   };

   inline vslice_datastream vslice::create_datastream() {
      return vslice_datastream(*this);
   }
}

BOOST_AUTO_TEST_SUITE(slice_tests)
   BOOST_FIXTURE_TEST_CASE(write_data_trace, test_fixture)
   {
      vslice vs;
      const auto offset = append_data_log( bt, vs );
      BOOST_REQUIRE_EQUAL(offset,0);

      const auto expected_offset = vs._pos;
      const auto offset2 = append_data_log( bt2, vs );
      BOOST_REQUIRE_EQUAL(offset2, expected_offset);

      vs._pos = offset;
      const auto bt_returned = extract_data_log<block_trace_v0>( vs );
      BOOST_REQUIRE(bt_returned == bt);

      vs._pos = offset2;
      const auto bt_returned2 = extract_data_log<block_trace_v0>( vs );
      BOOST_REQUIRE(bt_returned2 == bt2);
   }

   BOOST_FIXTURE_TEST_CASE(write_metadata_trace, test_fixture)
   {
      vslice vs;
      const auto offset = append_data_log( be1, vs );
      BOOST_REQUIRE_EQUAL(offset, 0);
      auto next_offset = vs._pos;
      const auto offset2 = append_data_log( le1, vs );
      BOOST_REQUIRE_EQUAL(offset2, next_offset);
      next_offset = vs._pos;
      const auto offset3 = append_data_log( be2, vs );
      BOOST_REQUIRE_EQUAL(offset3, next_offset);
      next_offset = vs._pos;
      const auto offset4 = append_data_log( le2, vs );
      BOOST_REQUIRE_EQUAL(offset4, next_offset);

      vs._pos = offset;
      const auto be_returned1 = extract_data_log<metadata_log_entry>( vs );
      BOOST_REQUIRE(be_returned1.contains<block_entry_v0>());
      const auto real_be_returned1 = be_returned1.get<block_entry_v0>();
      const auto real_be1 = be1.get<block_entry_v0>();
      BOOST_REQUIRE(real_be_returned1 == real_be1);

      vs._pos = offset2;
      const auto le_returned1 = extract_data_log<metadata_log_entry>( vs );
      BOOST_REQUIRE(le_returned1.contains<lib_entry_v0>());
      const auto real_le_returned1 = le_returned1.get<lib_entry_v0>();
      const auto real_le1 = le1.get<lib_entry_v0>();
      BOOST_REQUIRE(real_le_returned1 == real_le1);

      vs._pos = offset3;
      const auto be_returned2 = extract_data_log<metadata_log_entry>( vs );
      BOOST_REQUIRE(be_returned2.contains<block_entry_v0>());
      const auto real_be_returned2 = be_returned2.get<block_entry_v0>();
      const auto real_be2 = be2.get<block_entry_v0>();
      BOOST_REQUIRE(real_be_returned2 == real_be2);

      vs._pos = offset4;
      const auto le_returned2 = extract_data_log<metadata_log_entry>( vs );
      BOOST_REQUIRE(le_returned2.contains<lib_entry_v0>());
      const auto real_le_returned2 = le_returned2.get<lib_entry_v0>();
      const auto real_le2 = le2.get<lib_entry_v0>();
      BOOST_REQUIRE(real_le_returned2 == real_le2);
   }

   BOOST_FIXTURE_TEST_CASE(slice_number, test_fixture)
   {
      fc::temp_directory tempdir;
      boost::filesystem::path tempdir_path = tempdir.path();
      slice_provider sp(tempdir_path, 100);
      BOOST_REQUIRE_EQUAL(sp.slice_number(99), 0);
      BOOST_REQUIRE_EQUAL(sp.slice_number(100), 1);
      BOOST_REQUIRE_EQUAL(sp.slice_number(1599), 15);
      slice_provider sp2(tempdir_path, 0x10);
      BOOST_REQUIRE_EQUAL(sp2.slice_number(0xf), 0);
      BOOST_REQUIRE_EQUAL(sp2.slice_number(0x100), 0x10);
      BOOST_REQUIRE_EQUAL(sp2.slice_number(0x233), 0x23);
   }

   BOOST_FIXTURE_TEST_CASE(slice_file, test_fixture)
   {
      fc::temp_directory tempdir;
      bfs::path tempdir_path = tempdir.path();
      slice_provider sp(tempdir_path, 100);
      fc::cfile slice;

      const bool read_file = false;
      const bool append_file = true;
      // create trace slices
      for (uint i = 0; i < 9; ++i) {
         bool new_file = sp.find_trace_slice(i, append_file, slice);
         BOOST_REQUIRE(new_file);
         bfs::path fp = slice.get_file_path();
         BOOST_REQUIRE_EQUAL(fp.parent_path().generic_string(), tempdir.path().generic_string());
         const std::string expected_filename = "trace_0000000" + boost::lexical_cast<std::string>(i) + "00-0000000" + boost::lexical_cast<std::string>(i+1) + "00.log";
         BOOST_REQUIRE_EQUAL(fp.filename().generic_string(), expected_filename);
         BOOST_REQUIRE(slice.is_open());
         BOOST_REQUIRE_EQUAL(bfs::file_size(fp), 0);
         BOOST_REQUIRE_EQUAL(slice.tellp(), 0);
         slice.close();
      }

      // create trace index slices
      for (uint i = 0; i < 9; ++i) {
         bool new_file = sp.find_index_slice(i, append_file, slice);
         BOOST_REQUIRE(new_file);
         fc::path fp = slice.get_file_path();
         BOOST_REQUIRE_EQUAL(fp.parent_path().generic_string(), tempdir.path().generic_string());
         const std::string expected_filename = "trace_index_0000000" + boost::lexical_cast<std::string>(i) + "00-0000000" + boost::lexical_cast<std::string>(i+1) + "00.log";
         BOOST_REQUIRE_EQUAL(fp.filename().generic_string(), expected_filename);
         BOOST_REQUIRE(slice.is_open());
         slice_provider::index_header h;
         const auto data = fc::raw::pack(h);
         BOOST_REQUIRE_EQUAL(bfs::file_size(fp), data.size());
         BOOST_REQUIRE_EQUAL(slice.tellp(), data.size());
         slice.close();
      }

      // reopen trace slice for append
      bool new_file = sp.find_trace_slice(0, append_file, slice);
      BOOST_REQUIRE(!new_file);
      fc::path fp = slice.get_file_path();
      BOOST_REQUIRE_EQUAL(fp.parent_path().generic_string(), tempdir.path().generic_string());
      std::string expected_filename = "trace_0000000000-0000000100.log";
      BOOST_REQUIRE_EQUAL(fp.filename().generic_string(), expected_filename);
      BOOST_REQUIRE(slice.is_open());
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), 0);
      BOOST_REQUIRE_EQUAL(slice.tellp(), 0);
      uint64_t offset = store_provider::append_store(bt, slice);
      BOOST_REQUIRE_EQUAL(offset, 0);
      auto data = fc::raw::pack(bt);
      BOOST_REQUIRE(slice.tellp() > 0);
      BOOST_REQUIRE_EQUAL(data.size(), slice.tellp());
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), slice.tellp());
      uint64_t trace_file_size = bfs::file_size(fp);
      slice.close();

      // open same file for read
      new_file = sp.find_trace_slice(0, read_file, slice);
      BOOST_REQUIRE(!new_file);
      fp = slice.get_file_path();
      BOOST_REQUIRE_EQUAL(fp.filename().generic_string(), expected_filename);
      BOOST_REQUIRE(slice.is_open());
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), trace_file_size);
      BOOST_REQUIRE_EQUAL(slice.tellp(), 0);
      slice.close();

      // open same file for append again
      new_file = sp.find_trace_slice(0, append_file, slice);
      BOOST_REQUIRE(!new_file);
      fp = slice.get_file_path();
      BOOST_REQUIRE_EQUAL(fp.filename().generic_string(), expected_filename);
      BOOST_REQUIRE(slice.is_open());
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), trace_file_size);
      BOOST_REQUIRE_EQUAL(slice.tellp(), trace_file_size);
      slice.close();

      // reopen trace index slice for append
      new_file = sp.find_index_slice(1, append_file, slice);
      BOOST_REQUIRE(!new_file);
      fp = slice.get_file_path();
      BOOST_REQUIRE_EQUAL(fp.parent_path().generic_string(), tempdir.path().generic_string());
      expected_filename = "trace_index_0000000100-0000000200.log";
      BOOST_REQUIRE_EQUAL(fp.filename().generic_string(), expected_filename);
      BOOST_REQUIRE(slice.is_open());
      slice_provider::index_header h;
      data = fc::raw::pack(h);
      const uint64_t header_size = data.size();
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), header_size);
      BOOST_REQUIRE_EQUAL(slice.tellp(), header_size);
      offset = store_provider::append_store(be1, slice);
      BOOST_REQUIRE_EQUAL(offset, header_size);
      data = fc::raw::pack(be1);
      const auto be1_size = data.size();
      BOOST_REQUIRE_EQUAL(header_size + be1_size, slice.tellp());
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), slice.tellp());
      uint64_t index_file_size = bfs::file_size(fp);
      slice.close();

      new_file = sp.find_index_slice(1, read_file, slice);
      BOOST_REQUIRE(!new_file);
      fp = slice.get_file_path();
      BOOST_REQUIRE_EQUAL(fp.filename().generic_string(), expected_filename);
      BOOST_REQUIRE(slice.is_open());
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), header_size + be1_size);
      BOOST_REQUIRE_EQUAL(slice.tellp(), header_size);
      slice.close();

      new_file = sp.find_index_slice(1, append_file, slice);
      BOOST_REQUIRE(!new_file);
      fp = slice.get_file_path();
      BOOST_REQUIRE_EQUAL(fp.filename().generic_string(), expected_filename);
      BOOST_REQUIRE(slice.is_open());
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), header_size + be1_size);
      BOOST_REQUIRE_EQUAL(slice.tellp(), header_size + be1_size);
      offset = store_provider::append_store(le1, slice);
      BOOST_REQUIRE_EQUAL(offset, header_size + be1_size);
      data = fc::raw::pack(le1);
      const auto le1_size = data.size();
      BOOST_REQUIRE_EQUAL(header_size + be1_size + le1_size, slice.tellp());
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), slice.tellp());
      slice.close();

      new_file = sp.find_index_slice(1, read_file, slice);
      BOOST_REQUIRE(!new_file);
      fp = slice.get_file_path();
      BOOST_REQUIRE_EQUAL(fp.filename().generic_string(), expected_filename);
      BOOST_REQUIRE(slice.is_open());
      BOOST_REQUIRE_EQUAL(bfs::file_size(fp), header_size + be1_size + le1_size);
      BOOST_REQUIRE_EQUAL(slice.tellp(), header_size);
      slice.close();
   }

   BOOST_FIXTURE_TEST_CASE(store_provider_write_read, test_fixture)
   {
      fc::temp_directory tempdir;
      bfs::path tempdir_path = tempdir.path();
      store_provider sp(tempdir_path, 100);
      sp.append(bt);
      sp.append_lib(54);
      sp.append(bt2);
      uint64_t internal_offset = 0;
      uint64_t bt_bn = bt.number;
      bool found_block = false;
      bool lib_seen = false;
      uint64_t offset = sp.scan_metadata_log_from(9, 0, [&](const metadata_log_entry& e) -> bool {
         if (e.contains<block_entry_v0>()) {
            const auto& block = e.get<block_entry_v0>();
            if (block.number == bt_bn) {
               BOOST_REQUIRE(!found_block);
               found_block = true;
            }
         } else if (e.contains<lib_entry_v0>()) {
            auto best_lib = e.get<lib_entry_v0>();
            BOOST_REQUIRE(!lib_seen);
            BOOST_REQUIRE_EQUAL(best_lib.lib, 54);
            lib_seen = true;
            return false;
         }
         return true;
      });
      BOOST_REQUIRE(found_block);
      BOOST_REQUIRE(lib_seen);
   }

BOOST_AUTO_TEST_SUITE_END()
