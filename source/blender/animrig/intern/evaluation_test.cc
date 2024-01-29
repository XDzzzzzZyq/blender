/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "ANIM_animation.hh"
#include "ANIM_evaluation.hh"
#include "evaluation_internal.hh"

#include "BKE_animation.hh"
#include "BKE_animsys.h"
#include "BKE_idtype.hh"

#include "DNA_object_types.h"

#include "RNA_access.hh"
#include "RNA_prototypes.h"

#include "BLI_math_base.h"
#include "BLI_string_utf8.h"

#include <optional>

#include "testing/testing.h"

namespace blender::animrig::tests {

using namespace blender::animrig::internal;

class AnimationEvaluationTest : public testing::Test {
 protected:
  Animation anim = {};
  Object cube = {};
  Output *out;
  Layer *layer;

  KeyframeSettings settings = get_keyframe_settings(false);
  AnimationEvalContext anim_eval_context = {};
  PointerRNA cube_rna_ptr;

 public:
  static void SetUpTestSuite()
  {
    /* To make id_can_have_animdata() and friends work, the `id_types` array needs to be set up. */
    BKE_idtype_init();
  }

  void SetUp() override
  {
    STRNCPY_UTF8(cube.id.name, "OBKüüübus");
    out = anim.output_add();
    out->assign_id(&cube.id);
    layer = anim.layer_add("Kübus layer");

    /* Make it easier to predict test values. */
    settings.interpolation = BEZT_IPO_LIN;

    cube_rna_ptr = RNA_pointer_create(&cube.id, &RNA_Object, &cube.id);
  }

  void TearDown() override
  {
    BKE_animation_free_data(&anim);
  }

  /** Evaluate the layer, and return result for the given property. */
  std::optional<float> evaluate_single_property(const StringRef rna_path,
                                                const int array_index,
                                                const float eval_time)
  {
    anim_eval_context.eval_time = eval_time;
    EvaluationResult result = evaluate_layer(
        &cube_rna_ptr, *layer, out->stable_index, anim_eval_context);

    const AnimatedProperty *loc0_result = result.lookup_ptr(PropIdentifier(rna_path, array_index));
    if (!loc0_result) {
      return {};
    }
    return loc0_result->value;
  }

  /** Evaluate the layer, and test that the given property evaluates to the expected value. */
  testing::AssertionResult test_evaluate_layer(const StringRef rna_path,
                                               const int array_index,
                                               const float2 eval_time__expect_value)
  {
    const float eval_time = eval_time__expect_value[0];
    const float expect_value = eval_time__expect_value[1];

    const std::optional<float> opt_eval_value = evaluate_single_property(
        rna_path, array_index, eval_time);
    if (!opt_eval_value) {
      return testing::AssertionFailure()
             << rna_path << "[" << array_index << "] should have been animated";
    }

    const float eval_value = *opt_eval_value;
    const uint diff_ulps = ulp_diff_ff(expect_value, eval_value);
    if (diff_ulps >= 4) {
      return testing::AssertionFailure()
             << std::endl
             << "    " << rna_path << "[" << array_index
             << "] evaluation did not produce the expected result:" << std::endl
             << "      evaluted to: " << testing::PrintToString(eval_value) << std::endl
             << "      expected   : " << testing::PrintToString(expect_value) << std::endl;
    }

    return testing::AssertionSuccess();
  };

  /** Evaluate the layer, and test that the given property is not part of the result. */
  testing::AssertionResult test_evaluate_layer_no_result(const StringRef rna_path,
                                                         const int array_index,
                                                         const float eval_time)
  {
    const std::optional<float> eval_value = evaluate_single_property(
        rna_path, array_index, eval_time);
    if (eval_value) {
      return testing::AssertionFailure()
             << std::endl
             << "    " << rna_path << "[" << array_index
             << "] evaluation should NOT produce a value:" << std::endl
             << "      evaluted to: " << testing::PrintToString(*eval_value) << std::endl;
    }

    return testing::AssertionSuccess();
  }
};

TEST_F(AnimationEvaluationTest, evaluate_layer__keyframes)
{
  Strip *strip = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  KeyframeStrip &key_strip = strip->as<KeyframeStrip>();

  /* Set some keys. */
  key_strip.keyframe_insert(*out, "location", 0, {1.0f, 47.1f}, settings);
  key_strip.keyframe_insert(*out, "location", 0, {5.0f, 47.5f}, settings);
  key_strip.keyframe_insert(*out, "rotation_euler", 1, {1.0f, 0.0f}, settings);
  key_strip.keyframe_insert(*out, "rotation_euler", 1, {5.0f, 3.14f}, settings);

  /* Set the animated properties to some values. These should not be overwritten
   * by the evaluation itself. */
  cube.loc[0] = 3.0f;
  cube.loc[1] = 2.0f;
  cube.loc[2] = 7.0f;
  cube.rot[0] = 3.0f;
  cube.rot[1] = 2.0f;
  cube.rot[2] = 7.0f;

  /* Evaluate. */
  anim_eval_context.eval_time = 3.0f;
  EvaluationResult result = evaluate_layer(
      &cube_rna_ptr, *layer, out->stable_index, anim_eval_context);

  /* Check the result. */
  ASSERT_FALSE(result.is_empty());
  AnimatedProperty *loc0_result = result.lookup_ptr(PropIdentifier("location", 0));
  ASSERT_NE(nullptr, loc0_result) << "location[0] should have been animated";
  EXPECT_EQ(47.3f, loc0_result->value);

  EXPECT_EQ(3.0f, cube.loc[0]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(2.0f, cube.loc[1]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(7.0f, cube.loc[2]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(3.0f, cube.rot[0]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(2.0f, cube.rot[1]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(7.0f, cube.rot[2]) << "Evaluation should not modify the animated ID";
}

TEST_F(AnimationEvaluationTest, strip_boundaries__single_strip)
{
  /* Single finite strip, check first, middle, and last frame. */
  Strip *strip = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  strip->resize(1.0f, 10.0f);

  /* Set some keys. */
  KeyframeStrip &key_strip = strip->as<KeyframeStrip>();
  key_strip.keyframe_insert(*out, "location", 0, {1.0f, 47.0f}, settings);
  key_strip.keyframe_insert(*out, "location", 0, {5.0f, 327.0f}, settings);
  key_strip.keyframe_insert(*out, "location", 0, {10.0f, 48.0f}, settings);

  /* Evaluate the layer to see how it handles the boundaries + something in between. */
  EXPECT_TRUE(test_evaluate_layer("location", 0, {1.0f, 47.0f}));
  EXPECT_TRUE(test_evaluate_layer("location", 0, {3.0f, 187.0f}));
  EXPECT_TRUE(test_evaluate_layer("location", 0, {10.0f, 48.0f}));

  EXPECT_TRUE(test_evaluate_layer_no_result("location", 0, 10.001f));
}

TEST_F(AnimationEvaluationTest, strip_boundaries__nonoverlapping)
{
  /* Two finite strips that are strictly distinct. */
  Strip *strip1 = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  Strip *strip2 = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  strip1->resize(1.0f, 10.0f);
  strip2->resize(11.0f, 20.0f);
  strip2->frame_offset = 10;

  /* Set some keys. */
  {
    KeyframeStrip &key_strip1 = strip1->as<KeyframeStrip>();
    key_strip1.keyframe_insert(*out, "location", 0, {1.0f, 47.0f}, settings);
    key_strip1.keyframe_insert(*out, "location", 0, {5.0f, 327.0f}, settings);
    key_strip1.keyframe_insert(*out, "location", 0, {10.0f, 48.0f}, settings);
  }
  {
    KeyframeStrip &key_strip2 = strip2->as<KeyframeStrip>();
    key_strip2.keyframe_insert(*out, "location", 0, {1.0f, 47.0f}, settings);
    key_strip2.keyframe_insert(*out, "location", 0, {5.0f, 327.0f}, settings);
    key_strip2.keyframe_insert(*out, "location", 0, {10.0f, 48.0f}, settings);
  }

  /* Check Strip 1. */
  EXPECT_TRUE(test_evaluate_layer("location", 0, {1.0f, 47.0f}));
  EXPECT_TRUE(test_evaluate_layer("location", 0, {3.0f, 187.0f}));
  EXPECT_TRUE(test_evaluate_layer("location", 0, {10.0f, 48.0f}));

  /* Check Strip 2. */
  EXPECT_TRUE(test_evaluate_layer("location", 0, {11.0f, 47.0f}));
  EXPECT_TRUE(test_evaluate_layer("location", 0, {13.0f, 187.0f}));
  EXPECT_TRUE(test_evaluate_layer("location", 0, {20.0f, 48.0f}));

  /* Check outside the range of the strips. */
  EXPECT_TRUE(test_evaluate_layer_no_result("location", 0, 0.999f));
  EXPECT_TRUE(test_evaluate_layer_no_result("location", 0, 10.001f));
  EXPECT_TRUE(test_evaluate_layer_no_result("location", 0, 10.999f));
  EXPECT_TRUE(test_evaluate_layer_no_result("location", 0, 20.001f));
}

TEST_F(AnimationEvaluationTest, strip_boundaries__overlapping_edge)
{
  /* Two finite strips that are overlapping on their edge. */
  Strip *strip1 = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  Strip *strip2 = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  strip1->resize(1.0f, 10.0f);
  strip2->resize(10.0f, 19.0f);
  strip2->frame_offset = 9;

  /* Set some keys. */
  {
    KeyframeStrip &key_strip1 = strip1->as<KeyframeStrip>();
    key_strip1.keyframe_insert(*out, "location", 0, {1.0f, 47.0f}, settings);
    key_strip1.keyframe_insert(*out, "location", 0, {5.0f, 327.0f}, settings);
    key_strip1.keyframe_insert(*out, "location", 0, {10.0f, 48.0f}, settings);
  }
  {
    KeyframeStrip &key_strip2 = strip2->as<KeyframeStrip>();
    key_strip2.keyframe_insert(*out, "location", 0, {1.0f, 47.0f}, settings);
    key_strip2.keyframe_insert(*out, "location", 0, {5.0f, 327.0f}, settings);
    key_strip2.keyframe_insert(*out, "location", 0, {10.0f, 48.0f}, settings);
  }

  /* Check Strip 1. */
  EXPECT_TRUE(test_evaluate_layer("location", 0, {1.0f, 47.0f}));
  EXPECT_TRUE(test_evaluate_layer("location", 0, {3.0f, 187.0f}));

  /* Check overlapping frame. */
  EXPECT_TRUE(test_evaluate_layer("location", 0, {10.0f, 47.0f}))
      << "On the overlapping frame, only Strip 2 should be evaluated.";

  /* Check Strip 2. */
  EXPECT_TRUE(test_evaluate_layer("location", 0, {12.0f, 187.0f}));
  EXPECT_TRUE(test_evaluate_layer("location", 0, {19.0f, 48.0f}));

  /* Check outside the range of the strips. */
  EXPECT_TRUE(test_evaluate_layer_no_result("location", 0, 0.999f));
  EXPECT_TRUE(test_evaluate_layer_no_result("location", 0, 19.001f));
}

}  // namespace blender::animrig::tests