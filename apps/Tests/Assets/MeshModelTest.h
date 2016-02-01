// Test Cogwheel mesh models.
// ---------------------------------------------------------------------------
// Copyright (C) 2015-2016, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#ifndef _COGWHEEL_ASSETS_MESH_MODEL_TEST_H_
#define _COGWHEEL_ASSETS_MESH_MODEL_TEST_H_

#include <Cogwheel/Assets/MeshModel.h>

#include <gtest/gtest.h>

namespace Cogwheel {
namespace Assets {

class Assets_MeshModels : public ::testing::Test {
protected:
    // Per-test set-up and tear-down logic.
    virtual void SetUp() {
        MeshModels::allocate(8u);
    }
    virtual void TearDown() {
        MeshModels::deallocate();
    }
};

TEST_F(Assets_MeshModels, resizing) {
    // Test that capacity can be increased.
    unsigned int largerCapacity = Meshes::capacity() + 4u;
    MeshModels::reserve(largerCapacity);
    EXPECT_GE(MeshModels::capacity(), largerCapacity);

    // Test that capacity won't be decreased.
    MeshModels::reserve(5u);
    EXPECT_GE(MeshModels::capacity(), largerCapacity);

    MeshModels::deallocate();
    EXPECT_LT(MeshModels::capacity(), largerCapacity);
}

TEST_F(Assets_MeshModels, sentinel_mesh) {
    MeshModels::UID sentinel_ID = MeshModels::UID::invalid_UID();

    EXPECT_FALSE(MeshModels::has(sentinel_ID));
    EXPECT_EQ(MeshModels::get_model(sentinel_ID).m_scene_node_ID, Scene::SceneNodes::UID::invalid_UID());
    EXPECT_EQ(MeshModels::get_scene_node_ID(sentinel_ID), Scene::SceneNodes::UID::invalid_UID());
    EXPECT_EQ(MeshModels::get_model(sentinel_ID).m_mesh_ID, Meshes::UID::invalid_UID());
    EXPECT_EQ(MeshModels::get_mesh_ID(sentinel_ID), Meshes::UID::invalid_UID());
}

TEST_F(Assets_MeshModels, create) {
    Scene::SceneNodes::allocate(1u);
    Scene::SceneNodes::UID node_ID = Scene::SceneNodes::create("TestNode");
    Meshes::allocate(1u);
    Meshes::UID mesh_ID = Meshes::create("TestMesh", 16u);
    
    MeshModels::UID model_ID = MeshModels::create(node_ID, mesh_ID);

    EXPECT_TRUE(MeshModels::has(model_ID));
    EXPECT_EQ(MeshModels::get_model(model_ID).m_scene_node_ID, node_ID);
    EXPECT_EQ(MeshModels::get_scene_node_ID(model_ID), node_ID);
    EXPECT_EQ(MeshModels::get_model(model_ID).m_mesh_ID, mesh_ID);
    EXPECT_EQ(MeshModels::get_mesh_ID(model_ID), mesh_ID);

    Meshes::deallocate();
    Scene::SceneNodes::deallocate();
}

} // NS Assets
} // NS Cogwheel

#endif // _COGWHEEL_ASSETS_MESH_MODEL_TEST_H_