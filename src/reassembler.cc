#include "reassembler.hh"
#include "debug.hh"

using namespace std;

void Reassembler::insert( uint64_t first_index, std::string data, bool is_last_substring )
{
  Writer& writer = output_.writer();

  uint64_t first_unassembled_idx = writer.bytes_pushed();
  uint64_t first_unacceptable_idx = writer.available_capacity() + first_unassembled_idx;

  // 记录流结束下标
  if ( is_last_substring ) {
    last_index_ = std::min( last_index_, first_index + data.size() );
  }

  // 1. 丢弃超出容量的数据或空数据
  if ( first_index >= first_unacceptable_idx || data.empty() ) {
    debug( "Discarded segment: {}, {}", first_index, first_index + data.size() );
    // 处理空流或提前结束的情况
    if ( last_index_ != UINT64_MAX && writer.bytes_pushed() == last_index_ )
      writer.close();
    return;
  }

  // 2. 截断超出容量的数据
  if ( first_index + data.size() > first_unacceptable_idx )
    data = data.substr( 0, first_unacceptable_idx - first_index );

  // 3. 截断已写入部分
  if ( first_index + data.size() <= first_unassembled_idx ) {
    if ( last_index_ != UINT64_MAX && writer.bytes_pushed() == last_index_ )
      writer.close();
    return;
  }
  if ( first_index < first_unassembled_idx ) {
    data = data.substr( first_unassembled_idx - first_index );
    first_index = first_unassembled_idx;
  }

  // 4. 合并重叠区间
  uint64_t seg_start = first_index;
  uint64_t seg_end = first_index + data.size();
  storage_.insert( { { seg_start, seg_end }, data } );
  debug( "Inserted segment: {}, {}", seg_start, seg_end );

  for ( auto it : storage_ )
    debug( "Stored segment before merge: {}, {}", it.first.first, it.first.second );

  for ( auto it = storage_.begin(); it != storage_.end(); ) {
    auto next_it = next( it );
    if ( next_it == storage_.end() )
      break;
    uint64_t exist_start = it->first.first, exist_end = it->first.second;
    uint64_t next_start = next_it->first.first, next_end = next_it->first.second;
    std::string &exist_data = it->second, &next_data = next_it->second;

    if ( next_it->first.first <= it->first.second ) {
      if ( next_end > exist_end ) {
        exist_data += next_data.substr( exist_end - next_start );
        exist_end = next_end;
      }
      pair<pair<uint64_t, uint64_t>, string> new_data = { { exist_start, exist_end }, exist_data };
      storage_.erase( it );
      storage_.erase( next_it );
      storage_.insert( new_data );
      it = storage_.begin();
    } else
      it++;
  }

  for ( auto it : storage_ )
    debug( "Stored segment after merge: {}, {}", it.first.first, it.first.second );

  // 5. 尝试写入连续数据
  if ( storage_.empty() )
    return;
  if ( storage_.begin()->first.first == first_unassembled_idx ) {
    writer.push( storage_.begin()->second );
    storage_.erase( storage_.begin() );
  }

  // 6. 处理流结束
  if ( last_index_ != UINT64_MAX && writer.bytes_pushed() == last_index_ )
    writer.close();
}

uint64_t Reassembler::count_bytes_pending() const
{
  uint64_t cnt = 0;
  for ( const auto& kv : storage_ ) {
    cnt += kv.first.second - kv.first.first;
  }
  return cnt;
}