// Cogwheel scene node.
// ---------------------------------------------------------------------------
// Copyright (C) 2015, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#include <Cogwheel/Scene/SceneNode.h>

#include <assert.h>

namespace Cogwheel {
namespace Scene {

SceneNodes::UIDGenerator SceneNodes::m_UID_generator = UIDGenerator(0u);
std::string* SceneNodes::m_names = nullptr;

SceneNodes::UID* SceneNodes::m_parent_IDs = nullptr;
SceneNodes::UID* SceneNodes::m_sibling_IDs = nullptr;
SceneNodes::UID* SceneNodes::m_first_child_IDs = nullptr;

Math::Transform* SceneNodes::m_global_transforms = nullptr;

std::vector<SceneNodes::UID> SceneNodes::m_nodes_created = std::vector<SceneNodes::UID>(0);
std::vector<SceneNodes::UID> SceneNodes::m_nodes_destroyed = std::vector<SceneNodes::UID>(0);
std::vector<SceneNodes::UID> SceneNodes::m_transforms_changed = std::vector<SceneNodes::UID>(0);

void SceneNodes::allocate(unsigned int capacity) {
    if (is_allocated())
        return;

    m_UID_generator = UIDGenerator(capacity);
    capacity = m_UID_generator.capacity();
    
    m_names = new std::string[capacity];
    
    m_parent_IDs = new SceneNodes::UID[capacity];
    m_sibling_IDs = new SceneNodes::UID[capacity];
    m_first_child_IDs = new SceneNodes::UID[capacity];

    m_global_transforms = new Math::Transform[capacity];

    m_nodes_created.reserve(capacity / 4);
    m_nodes_destroyed.reserve(capacity / 4);
    m_transforms_changed.reserve(capacity / 4);

    // Allocate dummy element at 0.
    m_names[0] = "Dummy Node";
    m_parent_IDs[0] = m_first_child_IDs[0] = m_sibling_IDs[0] = UID::invalid_UID();
    m_global_transforms[0] = Math::Transform::identity();
}

void SceneNodes::deallocate() {
    if (!is_allocated())
        return;

    m_UID_generator = UIDGenerator(0u);
    delete[] m_names; m_names = nullptr;

    delete[] m_parent_IDs; m_parent_IDs = nullptr;
    delete[] m_sibling_IDs; m_sibling_IDs = nullptr;
    delete[] m_first_child_IDs; m_first_child_IDs = nullptr;

    delete[] m_global_transforms; m_global_transforms = nullptr;

    m_nodes_created.resize(0); m_nodes_created.shrink_to_fit();
    m_nodes_destroyed.resize(0); m_nodes_destroyed.shrink_to_fit();
    m_transforms_changed.resize(0); m_transforms_changed.shrink_to_fit();
}

void SceneNodes::reserve(unsigned int new_capacity) {
    unsigned int old_capacity = capacity();
    m_UID_generator.reserve(new_capacity);
    reserve_node_data(m_UID_generator.capacity(), old_capacity);
}

template <typename T>
static inline T* resize_and_copy_array(T* old_array, unsigned int new_capacity, unsigned int copyable_elements) {
    T* new_array = new T[new_capacity];
    std::copy(old_array, old_array + copyable_elements, new_array);
    delete[] old_array;
    return new_array;
}

void SceneNodes::reserve_node_data(unsigned int new_capacity, unsigned int old_capacity) {
    assert(m_first_child_IDs != nullptr);
    assert(m_global_transforms != nullptr);
    assert(m_names != nullptr);
    assert(m_parent_IDs != nullptr);
    assert(m_sibling_IDs != nullptr);

    const unsigned int copyable_elements = new_capacity < old_capacity ? new_capacity : old_capacity;

    m_names = resize_and_copy_array(m_names, new_capacity, copyable_elements);

    m_parent_IDs = resize_and_copy_array(m_parent_IDs, new_capacity, copyable_elements);
    m_sibling_IDs = resize_and_copy_array(m_sibling_IDs, new_capacity, copyable_elements);
    m_first_child_IDs = resize_and_copy_array(m_first_child_IDs, new_capacity, copyable_elements);

    m_global_transforms = resize_and_copy_array(m_global_transforms, new_capacity, copyable_elements);
}

SceneNodes::UID SceneNodes::create(const std::string& name, Math::Transform transform) {
    assert(m_first_child_IDs != nullptr);
    assert(m_global_transforms != nullptr);
    assert(m_names != nullptr);
    assert(m_parent_IDs != nullptr);
    assert(m_sibling_IDs != nullptr);

    unsigned int old_capacity = m_UID_generator.capacity();
    UID id = m_UID_generator.generate();
    if (old_capacity != m_UID_generator.capacity())
        // The capacity has changed and the size of all arrays need to be adjusted.
        reserve_node_data(m_UID_generator.capacity(), old_capacity);

    m_names[id] = name;
    m_parent_IDs[id] = m_first_child_IDs[id] = m_sibling_IDs[id] = UID::invalid_UID();
    m_global_transforms[id] = transform;

    m_nodes_created.push_back(id);

    return id;
}

void SceneNodes::destroy(SceneNodes::UID node_ID) {
    // We don't actually destroy anything when destroying a node. The properties will get overwritten later when a node is created in same the spot.
    if (m_UID_generator.erase(node_ID))
        m_nodes_destroyed.push_back(node_ID);
}

void SceneNodes::set_parent(SceneNodes::UID node_ID, const SceneNodes::UID parent_ID) {
    assert(m_parent_IDs != nullptr);
    assert(m_sibling_IDs != nullptr);
    assert(m_first_child_IDs != nullptr);

    SceneNodes::UID old_parent_ID = m_parent_IDs[node_ID];
    if (node_ID != parent_ID && node_ID != UID::invalid_UID()) {
        // Detach current node from it's place in the hierarchy
        if (old_parent_ID != UID::invalid_UID()) {
            // Find old previous sibling.
            UID sibling = m_first_child_IDs[old_parent_ID];
            if (sibling == node_ID) {
                m_first_child_IDs[old_parent_ID] = m_sibling_IDs[sibling];
            } else {
                while (node_ID != m_sibling_IDs[sibling])
                    sibling = m_sibling_IDs[sibling];
                m_sibling_IDs[sibling] = m_sibling_IDs[node_ID];
            }
        }

        // Attach it to the new parent as the first child and link it to the other siblings.
        m_parent_IDs[node_ID] = parent_ID;
        m_sibling_IDs[node_ID] = m_first_child_IDs[parent_ID];
        m_first_child_IDs[parent_ID] = node_ID;
    }
}

std::vector<SceneNodes::UID> SceneNodes::get_sibling_IDs(SceneNodes::UID node_ID) {
    assert(m_parent_IDs != nullptr);

    SceneNodes::UID parent_ID = m_parent_IDs[node_ID];
    
    return get_children_IDs(parent_ID);
}

std::vector<SceneNodes::UID> SceneNodes::get_children_IDs(SceneNodes::UID node_ID) {
    assert(m_first_child_IDs != nullptr);
    assert(m_sibling_IDs != nullptr);

    std::vector<SceneNodes::UID> res(0);
    SceneNodes::UID child = m_first_child_IDs[node_ID];
    while (child != UID::invalid_UID()) {
        res.push_back(child);
        child = m_sibling_IDs[child];
    }
    return res;
}

bool SceneNodes::has_child(SceneNodes::UID node_ID, SceneNodes::UID tested_Child_ID) {
    assert(m_first_child_IDs != nullptr);
    assert(m_sibling_IDs != nullptr);

    SceneNodes::UID child = m_first_child_IDs[node_ID];
    while (child != UID::invalid_UID()) {
        if (child == tested_Child_ID)
            return true;
        child = m_sibling_IDs[child];
    }
    return false;
}

std::vector<SceneNode> SceneNode::get_children() const {
    std::vector<SceneNodes::UID> children_IDs = SceneNodes::get_children_IDs(m_ID);
    std::vector<SceneNode> children;
    children.reserve(children_IDs.size());
    for (SceneNodes::UID id : children_IDs)
        children.push_back(SceneNode(id));
    return children;
}

Math::Transform SceneNodes::get_local_transform(SceneNodes::UID node_ID) {
    UID parent_ID = m_parent_IDs[node_ID];
    const Math::Transform parent_transform = m_global_transforms[parent_ID];
    const Math::Transform transform = m_global_transforms[node_ID];
    return Math::Transform::delta(parent_transform, transform);
}

void SceneNodes::set_local_transform(SceneNodes::UID node_ID, Math::Transform transform) {
    assert(m_global_transforms != nullptr);
    assert(m_parent_IDs != nullptr);

    if (node_ID == UID::invalid_UID()) return;

    // Update global transform.
    UID parent_ID = m_parent_IDs[node_ID];
    Math::Transform parent_transform = m_global_transforms[parent_ID];
    Math::Transform new_global_transform = parent_transform * transform;
    set_global_transform(node_ID, new_global_transform);
}

void SceneNodes::set_global_transform(SceneNodes::UID node_ID, Math::Transform transform) {
    assert(m_global_transforms != nullptr);

    if (node_ID == UID::invalid_UID()) return;
    
    Math::Transform delta_transform = Math::Transform::delta(m_global_transforms[node_ID], transform);
    m_global_transforms[node_ID] = transform;
    m_transforms_changed.push_back(node_ID);

    // Update global transforms of all children.
    apply_to_children_recursively(node_ID, [=](SceneNodes::UID child_ID) {
        m_global_transforms[child_ID] = delta_transform * m_global_transforms[child_ID];
        m_transforms_changed.push_back(child_ID);
    });
}

void SceneNodes::reset_change_notifications() {
    m_nodes_created.resize(0);
    m_nodes_destroyed.resize(0);
    m_transforms_changed.resize(0);
}

} // NS Scene
} // NS Cogwheel