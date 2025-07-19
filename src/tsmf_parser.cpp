// SPDX-License-Identifier: GPL-2.0-only
/*
	tsmf_parser.cpp
*/

#include "tsmf_parser.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>
#include <queue>
#include <type_traits>
#include <vector>


namespace ts
{

// 相対ストリーム番号に対する有効、無効状態を返す
bool TsmfParser::get_stream_status(std::vector<uint8_t>& stream_status) const
{
	static constexpr int32_t STREAM_STATUS_SIZE = 15;

	auto has_tsmf = false;
	stream_status.resize(STREAM_STATUS_SIZE);
	for (auto i = 0; i < STREAM_STATUS_SIZE; i++)
	{
		stream_status.at(i) = tsmf_data_.stream_info[i].stream_status;
		if (stream_status.at(i)) { has_tsmf = true; }
	}

	return has_tsmf;
}

// キューに格納されている要素の個数
size_t TsmfParser::size()
{
	std::lock_guard<std::mutex> lock(mutex_);
	return ts_queue_.size();
}

// キューに格納されているTSサイズ
size_t TsmfParser::stream_size()
{
	std::lock_guard<std::mutex> lock(mutex_);
	return ts_queue_.size() * ts_buffer_size_;
}

// 相対TS番号が有効である
bool TsmfParser::has_tsmf_number() const
{
	return (tsmf_number_ >= 1 && tsmf_number_ <= 15) ? true : false;
}

// TSバッファサイズの変更
void TsmfParser::set_ts_buffer_size(size_t size)
{
	// TS_PACKET_SIZE(TSパケットサイズ)の倍数に設定する
	ts_buffer_size_ = (static_cast<size_t>(size + TS_PACKET_SIZE - 1) / TS_PACKET_SIZE) * TS_PACKET_SIZE;
}

// キューサイズの変更
void TsmfParser::set_ts_queue_size(size_t size)
{
	ts_queue_size_ = size;
}

// 追加時タイムアウトの変更
void TsmfParser::set_timeout_push(const std::chrono::milliseconds& timeout)
{
	timeout_push_ = timeout;
}

// 取出し時タイムアウト時間の変更
void TsmfParser::set_timeout_pop(const std::chrono::milliseconds& timeout)
{
	timeout_pop_ = timeout;
}

// 相対TSMF番号による分離
void TsmfParser::set_tsmf_number(int32_t number)
{
	tsmf_number_ = number;
}

// データ初期化
void TsmfParser::clear()
{
	std::lock_guard<std::mutex> lock(mutex_);
	result_code_ = Result::None;
	is_abort_ = false;
	slot_counter_ = -1;
	tsmf_number_ = -1;
	tsmf_data_.clear();
	std::queue<std::unique_ptr<std::vector<uint8_t>>>().swap(ts_queue_);
	ts_buffer_.reset();
	ts_buffer_data_size_ = 0;
}

// 処理の中断
void TsmfParser::abort()
{
	std::lock_guard<std::mutex> lock(mutex_);
	is_abort_ = true;
	cv_push_.notify_all();
	cv_pop_.notify_all();
}

// データ追加処理
bool TsmfParser::push(std::unique_ptr<std::vector<uint8_t>>& stream_buffer)
{
	std::unique_lock<std::mutex> lock(mutex_);

	// 入力TSサイズ
	if (stream_buffer->size() < TS_PACKET_SIZE) { return false; }

	// キューに空きができる、中断フラグが設定される、タイムアウト（秒）するまで待つ
	auto ret = cv_push_.wait_for(lock, timeout_push_, [this]() { return (ts_queue_.size() < ts_queue_size_) || is_abort_; });

	// 中断フラグが設定された
	if (is_abort_)
	{
		result_code_ = Result::Abort;
		return false;
	}

	// キューが満杯でタイムアウト
	if (!ret)
	{
		result_code_ = Result::Timeout;
		return false;
	}

	result_code_ = Result::None;

	// 相対TS番号が範囲外の場合は入力されたTSデータをそのまま移動
	if (!has_tsmf_number())
	{
		ts_queue_.push(std::move(stream_buffer));
		result_code_ = Result::Ok;
		return true;
	}

	return parse_ts_buffer(*stream_buffer);
}

// データ取り出し処理
bool TsmfParser::pop(std::unique_ptr<std::vector<uint8_t>>& stream_buffer)
{
	std::unique_lock<std::mutex> lock(mutex_);

	// キューが空でなくなる、中断フラグが設定される、タイムアウトするまで待つ
	auto ret = cv_push_.wait_for(lock, timeout_pop_, [this]() { return !ts_queue_.empty() || is_abort_; });

	// 中断フラグ
	if (is_abort_)
	{
		result_code_ = Result::Abort;
		return false;
	}

	// タイムアウト
	if (!ret)
	{
		result_code_ = Result::Timeout;
		return false;
	}

	// キューが満杯なら今回空きができるので通知
	auto is_notify = (ts_queue_.size() == ts_queue_size_);

	stream_buffer = std::move(ts_queue_.front());
	ts_queue_.pop();

	// キューに空きができたので通知
	if (is_notify) { cv_push_.notify_one(); }

	result_code_ = Result::Ok;
	return true;
}

// TSバッファのTSMF処理を行う
bool TsmfParser::parse_ts_buffer(const std::vector<uint8_t>& stream_buffer)
{
	// 入力TSは188byte単位で同期済み
	if ((stream_buffer.size() < TS_PACKET_SIZE)
		|| (stream_buffer.size() % TS_PACKET_SIZE != 0)) { return false; }

	const auto* p = stream_buffer.data();
	const auto tail = p + stream_buffer.size();

	// 今回キューが空かどうか
	auto is_empty = ts_queue_.empty();

	// バッファを処理
	while (p < tail)
	{
		if (parse_tsmf_packet(p, tail - p))
		{
			// 残すスロットの場合

			if (!ts_buffer_)
			{
				// バッファを確保
				ts_buffer_ = std::make_unique<std::vector<uint8_t>>(ts_buffer_size_);
				ts_buffer_data_size_ = 0;
			}

			if (ts_buffer_data_size_ < ts_buffer_->size())
			{
				// 残すスロットをバッファへコピー
				std::copy(p, p + TS_PACKET_SIZE, ts_buffer_->begin() + ts_buffer_data_size_);
				ts_buffer_data_size_ += TS_PACKET_SIZE;
			}
				
			if (ts_buffer_data_size_ == ts_buffer_->size())
			{
				// バッファフルまでコピーしたらキューへ移動
				ts_queue_.push(std::move(ts_buffer_));

				// 今回キューが空でなくなったので通知
				if (is_empty)
				{
					cv_pop_.notify_one();
				}

				result_code_ = Result::Ok;
			}
		}

		// 次の位置へ
		p += TS_PACKET_SIZE;
	}

	return true;
}

// TSMFヘッダの解析を行う
bool TsmfParser::parse_tsmf_header(const uint8_t* buffer, size_t size)
{
	static constexpr uint16_t FRAME_SYNC_MASK = 0x1fff;
	static constexpr uint16_t FRAME_SYNC_F = 0x1a86;
	static constexpr uint16_t FRAME_SYNC_I = ~FRAME_SYNC_F & FRAME_SYNC_MASK;

	// パケットサイズ
	if (size < TS_PACKET_SIZE) { return false; }

	// 同期バイト
	auto sync_byte = buffer[0];
	if (sync_byte != TS_PACKET_SYNC_BYTE) { return false; }

	// 多重フレームPID
	uint16_t frame_PID = ((buffer[1] & 0x1f) << 8) | buffer[2];
	if (frame_PID != 0x002f) { return false; }

	// 固定値
	if ((buffer[3] & 0xf0) != 0x10) { return false; }

	// 多重フレーム同期信号
	uint16_t frame_sync = ((buffer[4] << 8) | buffer[5]) & FRAME_SYNC_MASK;
	if (frame_sync != FRAME_SYNC_F && frame_sync != FRAME_SYNC_I) { return false; }

	// CRC
	if (crc32(&buffer[4], static_cast<size_t>(TS_PACKET_SIZE) - 4) != 0) { return false; }

	// 連続性指標
	tsmf_data_.continuity_counter = buffer[3] & 0x0f;

	// 変更指示
	tsmf_data_.version_number = (buffer[6] & 0xe0) >> 5;

	// スロット配置法の区別
	tsmf_data_.relative_stream_number_mode = (buffer[6] & 0x10) >> 4;
	if (tsmf_data_.relative_stream_number_mode != 0x0) { return false; }

	// 多重フレーム形式
	tsmf_data_.frame_type = (buffer[6] & 0x0f);
	if (tsmf_data_.frame_type != 0x1) { return false; }

	// 相対ストリーム番号毎の情報
	for (auto i = 0; i < 15; i++)
	{
		// 相対ストリーム番号に対する有効、無効指示
		tsmf_data_.stream_info[i].stream_status = (buffer[7 + (i / 8)] & (0x80 >> (i % 8))) >> (7 - (i % 8));
		// ストリーム識別／相対ストリーム番号対応情報
		tsmf_data_.stream_info[i].stream_id = (buffer[9 + (i * 4)] << 8) | buffer[10 + (i * 4)];
		// オリジナルネットワ－ク識別／相対ストリーム番号対応情報
		tsmf_data_.stream_info[i].original_network_id = (buffer[11 + (i * 4)] << 8) | buffer[12 + (i * 4)];
		// 受信状態
		tsmf_data_.stream_info[i].receive_status = (buffer[69 + (i / 4)] & (0xc0 >> ((i % 4) * 2))) >> ((3 - (i % 4)) * 2);
	}

	// 緊急警報指示
	tsmf_data_.emergency_indicator = buffer[72] & 0x01;

	// 相対ストリーム番号対スロット対応情報
	for (auto i = 0; i < 52; i++)
	{
		tsmf_data_.relative_stream_number[i] = (buffer[73 + (i / 2)] & (0xf0 >> ((i % 2) * 4))) >> ((1 - (i % 2)) * 4);
	}

	return true;
}

// TSMF1パケット(1フレーム)の処理を行う
bool TsmfParser::parse_tsmf_packet(const uint8_t* buffer, size_t size)
{
	if (buffer[0] != TS_PACKET_SYNC_BYTE)
	{
		// TSパケットの同期外れ
		slot_counter_ = -1;
		return false;
	}

	// 相対TS番号が範囲外の場合は全てのスロットを返す
	if (tsmf_number_ < 1 || tsmf_number_ > 15) { return true; }

	// TSMF多重フレームヘッダ
	if (parse_tsmf_header(buffer, size))
	{
		slot_counter_ = 0;
		return false;
	}

	// TSMF多重フレームの同期がとれていない
	if (slot_counter_ < 0 || slot_counter_ > 51) { return false; }

	slot_counter_++;

	// その相対TS番号は未使用
	if (tsmf_data_.stream_info[tsmf_number_ - 1].stream_status == 0) { return false; }

	// このスロットは他の相対TS番号用スロットか未割当
	if (tsmf_data_.relative_stream_number[slot_counter_ - 1] != tsmf_number_) { return false; }

	return true;
}

// CRC32計算
uint32_t TsmfParser::crc32(const uint8_t* buffer, size_t size)
{
	static constexpr uint32_t crc_table[256] = {
		0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
		0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
		0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
		0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
		0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039, 0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
		0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
		0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
		0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
		0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
		0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
		0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
		0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
		0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
		0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
		0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
		0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
		0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
		0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
		0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
		0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
		0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
		0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
		0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
		0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
		0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
		0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
		0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
		0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
		0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
		0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
		0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
		0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4,
	};

	uint32_t c = 0xffffffff;

	for (size_t i = 0; i < size; i++)
	{
		c = (c << 8) ^ crc_table[((c >> 24) ^ buffer[i]) & 0xff];
	}

	return c;
}

}
