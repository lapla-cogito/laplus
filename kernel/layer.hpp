//重ね合わせ(レイヤー)処理
#pragma once
#include <memory>
#include <map>
#include <vector>
#include "graphics.hpp"
#include "window.hpp"
#include "message.hpp"

class Layer {
public:
	//指定IDのレイヤー生成
	Layer(unsigned int id = 0);
	//インスタンスIDを返す
	unsigned int ID() const;

	//ウィンドウを設定
	Layer& SetWindow(const std::shared_ptr<Window>& window);

	//設定されたウィンドウを返す
	std::shared_ptr<Window> GetWindow() const;

	//レイヤーの原点座標
	Vector2D<int> GetPosition() const;

	//trueにするとそのレイヤーはドラッグ移動可能
	Layer& SetDraggable(bool draggable);

	//レイヤーがドラッグ移動可能かどうか(trueなら可能)
	bool IsDraggable() const;

	//レイヤーの位置情報を指定された絶対座標へと更新.再描画無し
	Layer& Move(Vector2D<int> pos);

	//レイヤーの位置情報を指定された相対座標へと更新.再描画無し
	Layer& MoveRelative(Vector2D<int> pos_diff);

	//指定描画先にウィンドウの内容を描画
	void DrawTo(FrameBuffer& screen, const Rectangle<int>& area) const;

private:
	unsigned int id_;
	Vector2D<int> pos_{};
	std::shared_ptr<Window> window_{};
	bool draggable_{ false };
};

//複数レイヤー管理用のクラス
class LayerManager {
public:
	//描画する際の描画先を設定
	void SetWriter(FrameBuffer* screen);

	//新しいレイヤーを生成して参照を返す.新しく生成されたレイヤーの実体はLayerManager内部のコンテナで保持される
	Layer& NewLayer();

	//指定レイヤーの削除
	void RemoveLayer(unsigned int id);

	//指定レイヤーの非表示
	void Hide(unsigned int id);

	//現在表示状態のレイヤーを描画
	void Draw(const Rectangle<int>& area) const;

	//指定したレイヤーに設定されているウィンドウの描画領域内を再描画
	void Draw(unsigned int id) const;

	//指定したレイヤーに設定されているウィンドウ内の指定された範囲を再描画する
	void Draw(unsigned int id, Rectangle<int> area) const;

	//レイヤーの位置情報を指定された絶対座標へと更新して再描画する
	void Move(unsigned int id, Vector2D<int> new_pos);

	//レイヤーの位置情報を指定された相対座標へと更新して再描画する
	void MoveRelative(unsigned int id, Vector2D<int> pos_diff);

	//レイヤーの高さ方向の位置を指定された位置に移動する.new_heightに負数を与えるとレイヤーは非表示となり,0以上ならそれだけ移動.現在のレイヤー数以上の数値を指定した場合は最前面のレイヤーに
	void UpDown(unsigned int id, int new_height);

	//指定された座標にウィンドウを持つ最も上に表示されているレイヤーを返す
	Layer* FindLayerByPosition(Vector2D<int> pos, unsigned int exclude_id) const;

	//指定IDのレイヤーを返す
	Layer* FindLayer(unsigned int id);

	//指定レイヤーの高さを返す
	int GetHeight(unsigned int id);

private:
	FrameBuffer* screen_{ nullptr };
	mutable FrameBuffer back_buffer_{};
	std::vector<std::unique_ptr<Layer>> layers_{};
	std::vector<Layer*> layer_stack_{};
	unsigned int latest_id_{ 0 };
};

extern LayerManager* layer_manager;

class ActiveLayer {
public:
	ActiveLayer(LayerManager& manager);
	void SetMouseLayer(unsigned int mouse_layer);
	void Activate(unsigned int layer_id);
	unsigned int GetActive() const { return active_layer_; }

private:
	LayerManager& manager_;
	unsigned int active_layer_{ 0 };
	unsigned int mouse_layer_{ 0 };
};

extern ActiveLayer* active_layer;
extern std::map<unsigned int, uint64_t>* layer_task_map;

void InitializeLayer();
void ProcessLayerMessage(const Message& msg);

constexpr Message MakeLayerMessage(
	uint64_t task_id, unsigned int layer_id,
	LayerOperation op, const Rectangle<int>& area) {
	Message msg{ Message::kLayer, task_id };
	msg.arg.layer.layer_id = layer_id;
	msg.arg.layer.op = op;
	msg.arg.layer.x = area.pos.x;
	msg.arg.layer.y = area.pos.y;
	msg.arg.layer.w = area.size.x;
	msg.arg.layer.h = area.size.y;
	return msg;
}

Error CloseLayer(unsigned int layer_id);