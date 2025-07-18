// SPDX-License-Identifier: GPL-2.0-only
/*
	ts_sync_queue.h

	Copyright (C) 2024 hendecarows
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace ts
{

class SyncQueue
{
public:
	SyncQueue() = default;
	~SyncQueue() = default;

	enum class Result : int32_t
	{
		None,
		Ok,
		Abort,			// 中止
		Timeout,		// タイムアウト
	};

	Result result_code() const { return result_code_; }
	size_t get_ts_buffer_size() { return ts_buffer_size_; }
	size_t get_ts_queue_size() { return ts_queue_size_; }
	size_t size();														// キューに格納されている要素の個数
	size_t stream_size();												// キューに格納されているTSサイズ(要素×バッファサイズ)
	void set_ts_buffer_size(size_t size);
	void set_ts_queue_size(size_t size);
	void set_timeout_push(const std::chrono::milliseconds& timeout);
	void set_timeout_pop(const std::chrono::milliseconds& timeout);

	void clear();
	void abort();
	bool push(const std::vector<uint8_t>& stream_buffer);				// データ追加処理
	bool pop(std::unique_ptr<std::vector<uint8_t>>& stream_buffer);		// データ取出し処理

private:
	static constexpr uint8_t TS_SYNC_BYTE = 0x47;								// TS同期バイト: 0x47
	static constexpr uint32_t TS_PACKET_SIZE = 188;								// TSパケットサイズ: 188 byte
	static constexpr uint32_t TS_BUFFER_SIZE = TS_PACKET_SIZE * 1024;			// 同期済データバッファサイズ : 188 * 1024 = 192512 Byte
	static constexpr uint32_t TS_SYNC_PACKET = 2;								// 同期確認に用いるパケット数 : 2
	static constexpr uint32_t TS_SYNC_SIZE = TS_PACKET_SIZE * TS_SYNC_PACKET;	// 同期確認のパケットサイズ : 188 * 2 = 376 Byte
	static constexpr size_t TS_QUEUE_SIZE = 512;								// キューのデフォルトサイズ

	Result result_code_ = Result::None;								// 処理結果
	bool is_abort_ = false;											// 条件変数停止フラグ
	size_t ts_queue_size_ = TS_QUEUE_SIZE;							// キューサイズ
	size_t ts_buffer_size_ = TS_BUFFER_SIZE;						// TSバッファサイズ

	std::mutex mutex_;												// 排他処理用ミューテックス
	std::condition_variable cv_push_;								// 追加側条件変数
	std::condition_variable cv_pop_;								// 取出し側条件変数
	std::chrono::milliseconds timeout_push_{ 1000 };				// 追加時タイムアウト(msec)
	std::chrono::milliseconds timeout_pop_{ 1000 };					// 取出し時タイムアウト(msec)

	size_t ts_buffer_sync_size_ = 0;								// TSバッファ内データサイズ
	std::vector<uint8_t> ts_buffer_remain_;							// 残データバッファ
	std::unique_ptr<std::vector<uint8_t>> ts_buffer_sync_;			// 同期済データバッファ
	std::queue<std::unique_ptr<std::vector<uint8_t>>> ts_queue_;	// キュー
};

}

