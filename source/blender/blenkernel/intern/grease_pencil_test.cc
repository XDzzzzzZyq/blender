/* SPDX-FileCopyrightText: 2023 Blender Foundation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "testing/testing.h"

#include "BKE_curves.hh"
#include "BKE_grease_pencil.hh"
#include "BKE_idtype.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"

using namespace blender::bke::greasepencil;

namespace blender::bke::greasepencil::tests {

/* --------------------------------------------------------------------------------------------- */
/* Grease Pencil ID Tests. */

/* Note: Using a struct with constructor and destructor instead of a fixture here, to have all the
 * tests in the same group (`greasepencil`). */
struct GreasePencilIDTestContext {
  Main *bmain = nullptr;

  GreasePencilIDTestContext()
  {
    BKE_idtype_init();
    bmain = BKE_main_new();
  }
  ~GreasePencilIDTestContext()
  {
    BKE_main_free(bmain);
  }
};

TEST(greasepencil, create_grease_pencil_id)
{
  GreasePencilIDTestContext ctx;

  GreasePencil &grease_pencil = *static_cast<GreasePencil *>(BKE_id_new(ctx.bmain, ID_GP, "GP"));
  EXPECT_EQ(grease_pencil.drawings().size(), 0);
  EXPECT_EQ(grease_pencil.root_group.wrap().num_nodes_total(), 0);
}

/* --------------------------------------------------------------------------------------------- */
/* Drawing Array Tests. */

TEST(greasepencil, add_empty_drawings)
{
  GreasePencilIDTestContext ctx;
  GreasePencil &grease_pencil = *static_cast<GreasePencil *>(BKE_id_new(ctx.bmain, ID_GP, "GP"));
  grease_pencil.add_empty_drawings(3);
  EXPECT_EQ(grease_pencil.drawings().size(), 3);
}

TEST(greasepencil, remove_drawing)
{
  GreasePencilIDTestContext ctx;
  GreasePencil &grease_pencil = *static_cast<GreasePencil *>(BKE_id_new(ctx.bmain, ID_GP, "GP"));
  grease_pencil.add_empty_drawings(3);

  GreasePencilDrawing *drawing = reinterpret_cast<GreasePencilDrawing *>(
      grease_pencil.drawings_for_write()[1]);
  drawing->geometry.wrap().resize(0, 10);

  Layer &layer1 = grease_pencil.root_group.wrap().add_layer("Layer1");
  Layer &layer2 = grease_pencil.root_group.wrap().add_layer("Layer2");

  layer1.insert_frame(0, GreasePencilFrame{0});
  layer1.insert_frame(10, GreasePencilFrame{1});
  layer1.insert_frame(20, GreasePencilFrame{2});
  layer1.tag_frames_map_keys_changed();

  layer2.insert_frame(0, GreasePencilFrame{1});
  layer2.tag_frames_map_keys_changed();

  grease_pencil.remove_drawing(1);
  EXPECT_EQ(grease_pencil.drawings().size(), 2);

  static int expected_frames_size[] = {2, 0};
  static int expected_frames_pairs_layer0[][2] = {{0, 0}, {20, 1}};

  Span<const Layer *> layers = grease_pencil.layers();
  EXPECT_EQ(layers[0]->frames().size(), expected_frames_size[0]);
  EXPECT_EQ(layers[1]->frames().size(), expected_frames_size[1]);
  EXPECT_EQ(layers[0]->frames().lookup(expected_frames_pairs_layer0[0][0]).drawing_index,
            expected_frames_pairs_layer0[0][1]);
  EXPECT_EQ(layers[0]->frames().lookup(expected_frames_pairs_layer0[1][0]).drawing_index,
            expected_frames_pairs_layer0[1][1]);
}

TEST(greasepencil, overwrite_frame)
{
  Layer layer1("Layer1");

  layer1.insert_frame(0, GreasePencilFrame{0});
  layer1.tag_frames_map_keys_changed();

  EXPECT_EQ(layer1.frames().lookup(0).drawing_index, 0);

  layer1.overwrite_frame(0, GreasePencilFrame{42});
  EXPECT_EQ(layer1.frames().lookup(0).drawing_index, 42);
}

/* --------------------------------------------------------------------------------------------- */
/* Layer Tree Tests. */

TEST(greasepencil, layer_tree_empty)
{
  LayerGroup root;
}

TEST(greasepencil, layer_tree_build_simple)
{
  LayerGroup root;

  LayerGroup &group = root.add_group("Group1");
  group.add_layer("Layer1");
  group.add_layer("Layer2");
}

struct GreasePencilLayerTreeExample {
  StringRefNull names[7] = {"Group1", "Layer1", "Layer2", "Group2", "Layer3", "Layer4", "Layer5"};
  const bool is_layer[7] = {false, true, true, false, true, true, true};
  LayerGroup root;

  GreasePencilLayerTreeExample()
  {
    LayerGroup &group = root.add_group(names[0]);
    group.add_layer(names[1]);
    group.add_layer(names[2]);

    LayerGroup &group2 = group.add_group(names[3]);
    group2.add_layer(names[4]);
    group2.add_layer(names[5]);

    root.add_layer(names[6]);
  }
};

TEST(greasepencil, layer_tree_pre_order_iteration)
{
  GreasePencilLayerTreeExample ex;

  Span<const TreeNode *> children = ex.root.nodes();
  for (const int i : children.index_range()) {
    const TreeNode &child = *children[i];
    EXPECT_STREQ(child.name, ex.names[i].data());
  }
}

TEST(greasepencil, layer_tree_pre_order_iteration2)
{
  GreasePencilLayerTreeExample ex;

  Span<const Layer *> layers = ex.root.layers();
  char name[64];
  for (const int i : layers.index_range()) {
    const Layer &layer = *layers[i];
    snprintf(name, 64, "%s%d", "Layer", i + 1);
    EXPECT_STREQ(layer.name().data(), name);
  }
}

TEST(greasepencil, layer_tree_total_size)
{
  GreasePencilLayerTreeExample ex;
  EXPECT_EQ(ex.root.num_nodes_total(), 7);
}

TEST(greasepencil, layer_tree_node_types)
{
  GreasePencilLayerTreeExample ex;
  Span<const TreeNode *> children = ex.root.nodes();
  for (const int i : children.index_range()) {
    const TreeNode &child = *children[i];
    EXPECT_EQ(child.is_layer(), ex.is_layer[i]);
    EXPECT_EQ(child.is_group(), !ex.is_layer[i]);
  }
}

/* --------------------------------------------------------------------------------------------- */
/* Frames Tests. */

struct GreasePencilLayerFramesExample {
  /**
   *               | | | | | | | | | | |1|1|1|1|1|1|1|
   * Scene Frame:  |0|1|2|3|4|5|6|7|8|9|0|1|2|3|4|5|6|...
   * Drawing:      [#0       ][#1      ]   [#2     ]
   */
  const int sorted_keys[5] = {0, 5, 10, 12, 16};
  GreasePencilFrame sorted_values[5] = {{0}, {1}, {-1}, {2}, {-1}};
  Layer layer;

  GreasePencilLayerFramesExample()
  {
    for (int i = 0; i < 5; i++) {
      layer.frames_for_write().add(this->sorted_keys[i], this->sorted_values[i]);
    }
  }
};

TEST(greasepencil, frame_is_null)
{
  GreasePencilLayerFramesExample ex;
  EXPECT_TRUE(ex.layer.frames().lookup(10).is_null());
}

TEST(greasepencil, drawing_index_at)
{
  GreasePencilLayerFramesExample ex;
  EXPECT_EQ(ex.layer.drawing_index_at(-100), -1);
  EXPECT_EQ(ex.layer.drawing_index_at(100), -1);
  EXPECT_EQ(ex.layer.drawing_index_at(0), 0);
  EXPECT_EQ(ex.layer.drawing_index_at(1), 0);
  EXPECT_EQ(ex.layer.drawing_index_at(5), 1);
}

TEST(greasepencil, insert_frame)
{
  GreasePencilLayerFramesExample ex;
  GreasePencilFrame frame{3, 0, 0};
  EXPECT_FALSE(ex.layer.insert_frame(0, frame));
  EXPECT_TRUE(ex.layer.insert_frame(10, frame));
  EXPECT_EQ(ex.layer.drawing_index_at(10), 3);
  EXPECT_EQ(ex.layer.drawing_index_at(11), 3);
  EXPECT_EQ(ex.layer.drawing_index_at(12), 2);
}

TEST(greasepencil, insert_frame_duration_fail)
{
  GreasePencilLayerFramesExample ex;
  GreasePencilFrame frame{3, 0, 0};
  EXPECT_FALSE(ex.layer.insert_frame(0, 10, frame));
}

TEST(greasepencil, insert_frame_duration_override_start_null_frame)
{
  GreasePencilLayerFramesExample ex;
  GreasePencilFrame frame{3, 0, 0};
  EXPECT_TRUE(ex.layer.insert_frame(10, 2, frame));
  EXPECT_EQ(ex.layer.drawing_index_at(10), 3);
  EXPECT_EQ(ex.layer.drawing_index_at(11), 3);
  EXPECT_EQ(ex.layer.drawing_index_at(12), 2);
}

TEST(greasepencil, insert_frame_duration_check_duration)
{
  GreasePencilLayerFramesExample ex;
  GreasePencilFrame frame{3, 0, 0};
  EXPECT_TRUE(ex.layer.insert_frame(17, 10, frame));
  Span<int> sorted_keys = ex.layer.sorted_keys();
  EXPECT_EQ(sorted_keys.size(), 7);
  EXPECT_EQ(sorted_keys[6] - sorted_keys[5], 10);
}

TEST(greasepencil, insert_frame_duration_override_null_frames)
{
  Layer layer;
  layer.frames_for_write().add(0, {1});
  layer.frames_for_write().add(1, {-1});
  layer.frames_for_write().add(2, {-1});
  layer.frames_for_write().add(3, {-1});

  GreasePencilFrame frame{3, 0, 0};
  EXPECT_TRUE(layer.insert_frame(1, 10, frame));
  EXPECT_EQ(layer.drawing_index_at(0), 1);
  EXPECT_EQ(layer.drawing_index_at(1), 3);
  EXPECT_EQ(layer.drawing_index_at(11), -1);
  Span<int> sorted_keys = layer.sorted_keys();
  EXPECT_EQ(sorted_keys.size(), 3);
  EXPECT_EQ(sorted_keys[0], 0);
  EXPECT_EQ(sorted_keys[1], 1);
  EXPECT_EQ(sorted_keys[2], 11);
}

}  // namespace blender::bke::greasepencil::tests