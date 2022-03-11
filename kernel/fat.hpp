//FATファイル操作用のプログラム集
#pragma once
#include <cstdint>
#include <cstddef>
#include "error.hpp"
#include "file.hpp"

namespace fat {

	struct BPB {
		uint8_t jump_boot[3];
		char oem_name[8];
		uint16_t bytes_per_sector;
		uint8_t sectors_per_cluster;
		uint16_t reserved_sector_count;
		uint8_t num_fats;
		uint16_t root_entry_count;
		uint16_t total_sectors_16;
		uint8_t media;
		uint16_t fat_size_16;
		uint16_t sectors_per_track;
		uint16_t num_heads;
		uint32_t hidden_sectors;
		uint32_t total_sectors_32;
		uint32_t fat_size_32;
		uint16_t ext_flags;
		uint16_t fs_version;
		uint32_t root_cluster;
		uint16_t fs_info;
		uint16_t backup_boot_sector;
		uint8_t reserved[12];
		uint8_t drive_number;
		uint8_t reserved1;
		uint8_t boot_signature;
		uint32_t volume_id;
		char volume_label[11];
		char fs_type[8];
	} __attribute__((packed));

	enum class Attribute : uint8_t {
		kReadOnly = 0x01,
		kHidden = 0x02,
		kSystem = 0x04,
		kVolumeID = 0x08,
		kDirectory = 0x10,
		kArchive = 0x20,
		kLongName = 0x0f,
	};

	struct DirectoryEntry {
		unsigned char name[11];
		Attribute attr;
		uint8_t ntres;
		uint8_t create_time_tenth;
		uint16_t create_time;
		uint16_t create_date;
		uint16_t last_access_date;
		uint16_t first_cluster_high;
		uint16_t write_time;
		uint16_t write_date;
		uint16_t first_cluster_low;
		uint32_t file_size;

		uint32_t FirstCluster() const {
			return first_cluster_low |
				(static_cast<uint32_t>(first_cluster_high) << 16);
		}
	} __attribute__((packed));

	extern BPB* boot_volume_image;
	extern unsigned long bytes_per_cluster;
	void Initialize(void* volume_image);

	//指定されたクラスタの先頭セクタが置いてあるメモリ領域のアドレスを返す.clusterはクラスタ番号(2始まり)
	uintptr_t GetClusterAddr(unsigned long cluster);

	//指定されたクラスタの先頭セクタが置いてあるメモリ領域へのポインターを返す.clusterはクラスタ番号(2始まり)
	template <class T>
	T* GetSectorByCluster(unsigned long cluster) {
		return reinterpret_cast<T*>(GetClusterAddr(cluster));
	}

	//ディレクトリエントリの短名を基本名と拡張子名に分割して取得.パディングされた空白文字0x20は取り除かれ,null終端される.entryはファイル名を得る対象のディレクトリエントリ,baseは拡張子を除いたファイル名(9バイト以上の配列),extは拡張子(4バイト以上の配列)
	void ReadName(const DirectoryEntry& entry, char* base, char* ext);

	//ディレクトリエントリの短名をdestにコピーする.短名の拡張子が空なら "<base>" を,空でなければ "<base>.<ext>" をコピーする.entryはファイル名を得る対象のディレクトリエントリ,destは基本名と拡張子を結合した文字列を格納するに十分な大きさを持った配列
	void FormatName(const DirectoryEntry& entry, char* dest);

	static const unsigned long kEndOfClusterchain = 0x0ffffffflu;

	//指定されたクラスタの次のクラスタ番号を返す.clusterがクラスタ番号.次のクラスタ番号が存在しない場合はkEndOfClusterchainが返る
	unsigned long NextCluster(unsigned long cluster);

	//指定されたディレクトリからファイルを探索して,そのエントリと末尾スラッシュを示すフラグの組を返す.見つからなければnullptr.nameは8+3形式のファイル名(大文字小文字は区別しない)directory_clusterはディレクトリの開始クラスタ(省略するとルートディレクトリから検索する)
	//エントリの直後にスラッシュがあれば返るフラグはtrue
	//パスの途中のエントリがファイルであれば探索を諦めてそのエントリとtrueを返す
	std::pair<DirectoryEntry*, bool>
		FindFile(const char* path, unsigned long directory_cluster = 0);

	bool NameIsEqual(const DirectoryEntry& entry, const char* name);

	//指定されたファイルの内容をバッファへコピーして読み込んだバイト数を返す.bufはファイル内容の格納先,lenはバッファの大きさ(byte単位),entryはファイルを表すディレクトリエントリ
	size_t LoadFile(void* buf, size_t len, DirectoryEntry& entry);

	bool IsEndOfClusterchain(unsigned long cluster);

	uint32_t* GetFAT();

	//指定したクラスタ数だけクラスタチェーンを伸長して,その最後尾のクラスタ番号を返す.eoc_clusterは伸長したいクラスタチェーンに属するいずれかのクラスタ番号,nは伸長するクラスタ数
	unsigned long ExtendCluster(unsigned long eoc_cluster, size_t n);

	//指定したディレクトリの空きエントリを1つ返す.ディレクトリが満杯ならクラスタを1つ伸長して空きエントリを確保.dir_clusterが空きエントリを探すディレクトリ
	DirectoryEntry* AllocateEntry(unsigned long dir_cluster);

	//ディレクトリエントリに短ファイル名をセット.entryはファイル名を設定する対象のディレクトリエントリ,nameは基本名と拡張子をドットで結合したファイル名
	void SetFileName(DirectoryEntry& entry, const char* name);

	//指定されたパスにファイルエントリを作成して作成されたファイルエントリを返す.pathがファイルパス
	WithError<DirectoryEntry*> CreateFile(const char* path);

	//指定した数の空きクラスタからなるチェーンを構築して,そのチェーンの先頭クラスタ番号を返す.nがクラスタ数
	unsigned long AllocateClusterChain(size_t n);

	class FileDescriptor : public ::FileDescriptor {
	public:
		explicit FileDescriptor(DirectoryEntry& fat_entry);
		size_t Read(void* buf, size_t len) override;
		size_t Write(const void* buf, size_t len) override;
		size_t Size() const override { return fat_entry_.file_size; }
		size_t Load(void* buf, size_t len, size_t offset) override;

	private:
		DirectoryEntry& fat_entry_;
		size_t rd_off_ = 0;
		unsigned long rd_cluster_ = 0;
		size_t rd_cluster_off_ = 0;
		size_t wr_off_ = 0;
		unsigned long wr_cluster_ = 0;
		size_t wr_cluster_off_ = 0;
	};

}