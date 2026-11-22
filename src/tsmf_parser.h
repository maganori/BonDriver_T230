// SPDX-License-Identifier: GPL-2.0-only
/*
	tsmf_parser.h

	BonDriver_BDAのTSMF分離コードにキュー機能を追加
	https://github.com/radi-sh/BonDriver_BDA/blob/master/BonDriver_BDA/TSMF.h
	https://github.com/radi-sh/BonDriver_BDA/blob/master/BonDriver_BDA/TSMF.cpp
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

class TsmfParser
{
public:
	TsmfParser() = default;
	~TsmfParser() = default;

	enum class Result : int32_t
	{
		None,
		Ok,
		Abort,
		Timeout,
	};


	Result result_code() const { return result_code_; }					// キュー処理の結果
	size_t get_ts_buffer_size() const { return ts_buffer_size_; }		// TSバッファサイズ
	size_t get_ts_queue_size() const { return ts_queue_size_; }			// キューサイズ
	int32_t get_tsmf_number() const { return tsmf_number_; }			// 相対TS番号
	bool get_stream_status(std::vector<uint8_t>& stream_status) const;	// 相対ストリーム番号に対する有効、無効
	size_t size();														// キューに格納されている要素の個数
	size_t stream_size();												// キューに格納されているTSサイズ(要素×バッファサイズ)
	bool has_tsmf_number() const;										// 相対TS番号が有効
	void set_ts_buffer_size(size_t size);								// TSバッファサイズの変更
	void set_ts_queue_size(size_t size);								// キューサイズの変更
	void set_tsmf_number(int32_t index);								// 相対TS番号の設定
	void set_timeout_push(const std::chrono::milliseconds& timeout);	// キュー追加時タイムアウト時間を変更
	void set_timeout_pop(const std::chrono::milliseconds& timeout);		// キュー取出し時タイムアウト時間を変更

	void clear();														// キューの初期化
	void abort();														// キュー処理の終了
	bool push(std::unique_ptr<std::vector<uint8_t>>& stream_buffer);	// キューへのデータ追加
	bool pop(std::unique_ptr<std::vector<uint8_t>>& stream_buffer);		// キューからデータ取り出し

private:
	static constexpr uint8_t TS_PACKET_SYNC_BYTE = 0x47;				// TSパケットヘッダ同期バイトコード
	static constexpr uint32_t TS_PACKET_SIZE = 188;						// TSパケットサイズ
	static constexpr uint32_t TS_BUFFER_SIZE = TS_PACKET_SIZE * 1024;	// TSバッファサイズ
	static constexpr size_t TS_QUEUE_SIZE = 512;						// キューのデフォルトサイズ

	struct StreamInfo
	{
		uint8_t stream_status = 0;							// 相対ストリーム番号に対する有効、無効指示
		uint16_t stream_id = 0;								// ストリーム識別／相対ストリーム番号対応情報
		uint16_t original_network_id = 0;					// オリジナルネットワ－ク識別／相対ストリーム番号対応情報
		uint8_t receive_status = 0;							// 受信状態

		void clear()
		{
			stream_status = 0;
			stream_id = 0;
			original_network_id = 0;
			receive_status = 0;
		}
	};

	struct TSMFData
	{
		uint8_t continuity_counter = 0;						// 連続性指標
		uint8_t version_number = 0;							// 変更指示
		uint8_t relative_stream_number_mode = 0;			// スロット配置法の区別
		uint8_t frame_type = 0;								// 多重フレーム形式
		StreamInfo stream_info[15];							// 相対ストリーム番号毎の情報
		uint8_t emergency_indicator = 0;					// 緊急警報指示
		uint8_t relative_stream_number[52] = { 0 };			// 相対ストリーム番号対スロット対応情報

		void clear()
		{
			continuity_counter = 0;
			version_number = 0;
			relative_stream_number_mode = 0;
			frame_type = 0;
			emergency_indicator = 0;
			for (auto& p : stream_info) { p.clear(); }
			std::fill(relative_stream_number, relative_stream_number + sizeof(relative_stream_number), 0);
		}
	};

	Result result_code_ = Result::None;								// 処理結果
	bool is_abort_ = false;											// キュー用条件変数フラグ
	size_t ts_queue_size_ = TS_QUEUE_SIZE;							// キューサイズ
	size_t ts_buffer_size_ = TS_BUFFER_SIZE;						// 同期済バッファサイズ

	std::mutex mutex_;												// キュー用排他ミューテックス
	std::condition_variable cv_push_;								// キュー用PUSH側条件変数
	std::condition_variable cv_pop_;								// キュー用POP側条件変数
	std::chrono::milliseconds timeout_push_{ 1000 };				// キュー用PUSH時タイムアウト(msec)
	std::chrono::milliseconds timeout_pop_{ 1000 };					// キュー用POP時タイムアウト(msec)

	int32_t slot_counter_ = -1;										// TSMF多重フレームスロット番号
	int32_t tsmf_number_ = -1;										// 残すストリームの相対番号（-1は自動選択）

	// (patched) tsmf_number = -1 の自動選択用
	bool auto_mode_ = true;											// 自動選択モード（tsmf_number_が-1の間は有効）
	bool auto_decided_ = false;										// 自動選択の判定済みフラグ
	int32_t effective_tsmf_number_ = 0;								// 自動選択で決まった相対番号（0はパススルー）
	uint32_t auto_wait_packets_ = 0;								// 判定待ちで処理したパケット数
	static constexpr uint32_t AUTO_WAIT_PACKET_LIMIT = 512;			// TSMFヘッダを待つパケット数の上限
	TSMFData tsmf_data_;											// TSMF多重フレームヘッダ情報

	std::unique_ptr<std::vector<uint8_t>> ts_buffer_;				// TSMF分離後のTS保存用バッファ
	std::queue<std::unique_ptr<std::vector<uint8_t>>> ts_queue_;	// TSMF分離後のTS保存用キュー
	size_t ts_buffer_data_size_ = 0;								// TSバッファ内データサイズ

	bool use_parse_path() const;										// (patched) 分離処理を行うかどうか
	bool parse_ts_buffer(const std::vector<uint8_t>& stream_buffer);	// TSバッファに対するTSMF分離
	bool parse_tsmf_header(const uint8_t* buffer, size_t size);			// TSMFヘッダの解析
	bool parse_tsmf_packet(const uint8_t* buffer, size_t size);			// TSMFパケットの処理
	uint32_t crc32(const uint8_t* buffer, size_t size);					// CRC32計算
};

}
