// SPDX-License-Identifier: GPL-2.0-only
/*
	ts_sync_queue.cpp

	Copyright (C) 2024 hendecarows
*/

#include "ts_sync_queue.h"

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

// キューに格納されている要素の個数
size_t SyncQueue::size()
{
	std::lock_guard<std::mutex> lock(mutex_);
	return ts_queue_.size();
}

// キューに格納されているTSサイズ
size_t SyncQueue::stream_size()
{
	std::lock_guard<std::mutex> lock(mutex_);
	return ts_queue_.size() * ts_buffer_size_;
}

// TSバッファサイズの変更
void SyncQueue::set_ts_buffer_size(size_t size)
{
	// TS_SYNC_SIZE(同期を確認したパケットサイズ)の倍数に設定する
	ts_buffer_size_ = (static_cast<size_t>(size + TS_SYNC_SIZE - 1) / TS_SYNC_SIZE) * TS_SYNC_SIZE;
}

// キューサイズの変更
void SyncQueue::set_ts_queue_size(size_t size)
{
	ts_queue_size_ = size;
}

// 追加時タイムアウトを変更
void SyncQueue::set_timeout_push(const std::chrono::milliseconds& timeout)
{
	timeout_push_ = timeout;
}

// 取出し時タイムアウトを変更
void SyncQueue::set_timeout_pop(const std::chrono::milliseconds& timeout)
{
	timeout_pop_ = timeout;
}

// データ初期化
void SyncQueue::clear()
{
	std::lock_guard<std::mutex> lock(mutex_);
	is_abort_ = false;
	ts_buffer_remain_.clear();
	ts_buffer_sync_size_ = 0;
	ts_buffer_sync_.reset();
	std::queue<std::unique_ptr<std::vector<uint8_t>>>().swap(ts_queue_);
}

// 中止処理
void SyncQueue::abort()
{
	std::lock_guard<std::mutex> lock(mutex_);
	is_abort_ = true;
	cv_push_.notify_all();
	cv_pop_.notify_all();
}

// データ追加処理
bool SyncQueue::push(const std::vector<uint8_t>& stream_buffer)
{
	std::unique_lock<std::mutex> lock(mutex_);

	// 入力TSサイズ
	if (stream_buffer.size() == 0) { return false; }

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

	// 今回キューが空かどうか
	auto is_empty = ts_queue_.empty();

	// 前回未処理のデータがある場合は新規データを連結する
	auto remaining_size = ts_buffer_remain_.size();
	ts_buffer_remain_.resize(remaining_size + stream_buffer.size());
	std::copy(stream_buffer.begin(), stream_buffer.end(), ts_buffer_remain_.begin() + remaining_size);

	// TSの同期を確認
	auto p = ts_buffer_remain_.cbegin();
	auto tail = ts_buffer_remain_.cend();
	while (p < tail - TS_SYNC_SIZE)
	{
		auto sync = 0;
		for (auto i = 0; i < TS_SYNC_SIZE; i += TS_PACKET_SIZE)
		{
			if ((*(p + i) == TS_SYNC_BYTE) && (*(p + i + TS_PACKET_SIZE) == TS_SYNC_BYTE))
			{
				sync++;
			}
			else
			{
				break;
			}
		}

		if (sync < TS_SYNC_PACKET)
		{
			// 同期 NG
			p++;
			continue;
		}

		// 同期 OK
		while (sync)
		{
			if (!ts_buffer_sync_)
			{
				// 同期済TS用バッファを確保
				ts_buffer_sync_ = std::make_unique<std::vector<uint8_t>>(ts_buffer_size_);
				ts_buffer_sync_size_ = 0;
			}

			// 同期済TSパケットをバッファにコピー
			std::copy(p, p + TS_PACKET_SIZE, ts_buffer_sync_->begin() + ts_buffer_sync_size_);
			ts_buffer_sync_size_ += TS_PACKET_SIZE;
			p += TS_PACKET_SIZE;
			sync--;

			if (ts_buffer_sync_->size() == ts_buffer_sync_size_)
			{
				// バッファサイズまで同期済TSパケットをコピーしたらキューに追加
				ts_queue_.push(std::move(ts_buffer_sync_));

				// 今回キューが空でなくなったので通知
				if (is_empty)
				{
					cv_pop_.notify_one();
				}

				result_code_ = Result::Ok;
			}
		}
	}

	// 未処理のTSをバッファにコピー
	if (p < tail)
	{
		ts_buffer_remain_.assign(p, tail);
	}
	else
	{
		ts_buffer_remain_.clear();
	}

	return true;
}

// データ取出し処理
bool SyncQueue::pop(std::unique_ptr<std::vector<uint8_t>>& stream_buffer)
{
	std::unique_lock<std::mutex> lock(mutex_);

	// キューが空でなくなる、中断フラグが設定される、タイムアウトするまで待つ
	// (patched) wait on cv_pop_ (data-available), not cv_push_ (space-available):
	// push() notifies cv_pop_ when the queue becomes non-empty.
	auto ret = cv_pop_.wait_for(lock, timeout_pop_, [this]() { return !ts_queue_.empty() || is_abort_; });

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

}
