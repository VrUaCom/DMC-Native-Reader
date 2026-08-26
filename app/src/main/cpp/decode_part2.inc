    }
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
    *out = v;
    return true;
}

[[nodiscard]] Vec3 bone_world(std::size_t index,
                              std::vector<BoneState>* bones) noexcept {
    if (bones == nullptr || index >= bones->size()) return {};
    auto& b = (*bones)[index];
    if (b.world_ready) return b.world;
    if (b.visiting) {
        b.world = b.local;
        b.world_ready = true;
        return b.world;
    }
    b.visiting = true;
    if (b.parent < 0 || b.parent == static_cast<int>(index) ||
        static_cast<std::size_t>(b.parent) >= bones->size()) {
        b.world = b.local;
    } else {
        b.world = add(bone_world(static_cast<std::size_t>(b.parent), bones), b.local);
    }
    b.visiting = false;
    b.world_ready = true;
    return b.world;
}

[[nodiscard]] bool parse_scm_object_translations(const BinaryReader& reader,
                                                 std::size_t skeleton_offset,
                                                 std::uint8_t bone_count,
                                                 std::size_t object_count,
                                                 std::vector<Vec3>* translations) {
    if (translations == nullptr) return false;
    translations->assign(object_count, Vec3{});
    if (bone_count == 0 || skeleton_offset == 0) return true;
    if (!reader.range(skeleton_offset, 16)) return false;

    std::int32_t h_rel = 0, order_rel = 0, child_rel = 0, trans_rel = 0;
    if (!reader.read_le(skeleton_offset + 0, &h_rel) ||
        !reader.read_le(skeleton_offset + 4, &order_rel) ||
        !reader.read_le(skeleton_offset + 8, &child_rel) ||
        !reader.read_le(skeleton_offset + 12, &trans_rel)) {
        return false;
    }
    const auto absolute = [skeleton_offset, &reader](std::int32_t rel,
                                                      std::size_t bytes,
                                                      std::size_t* out) -> bool {
        const std::int64_t a = static_cast<std::int64_t>(skeleton_offset) + rel;
        if (a < 0) return false;
        const auto u = static_cast<std::size_t>(a);
        if (!reader.range(u, bytes)) return false;
        *out = u;
        return true;
    };

    const std::size_t bc = bone_count;
    std::size_t h_off = 0, order_off = 0, child_off = 0, trans_off = 0;
    if (!absolute(h_rel, bc, &h_off) ||
        !absolute(order_rel, bc, &order_off) ||
        !absolute(child_rel, bc, &child_off) ||
        !absolute(trans_rel, bc * 0x20u, &trans_off)) {
        return false;
    }

    const auto normalize_i8_index = [](std::uint8_t raw) -> int {
        if (raw == 0xff) return -1;
        return static_cast<int>(raw);
    };
    std::vector<int> hierarchy(bc, -1), order(bc, -1), child(bc, -1);
    for (std::size_t i = 0; i < bc; ++i) {
        const auto* hp = reader.ptr(h_off + i, 1);
        const auto* op = reader.ptr(order_off + i, 1);
        const auto* cp = reader.ptr(child_off + i, 1);
        if (hp == nullptr || op == nullptr || cp == nullptr) return false;
        hierarchy[i] = normalize_i8_index(*hp);
        order[i] = normalize_i8_index(*op);
        child[i] = normalize_i8_index(*cp);
    }

    auto build_parent_map = [&](bool remap, std::vector<int>* map, int* invalid) {
        map->assign(bc, -1);
        *invalid = 0;
        for (std::size_t i = 0; i < bc; ++i) {
            const int bone_index = order[i];
            if (bone_index < 0 || static_cast<std::size_t>(bone_index) >= bc) {
                ++*invalid;
                continue;
            }
            const int parent_raw = hierarchy[i];
            int parent_id = -1;
            if (parent_raw != -1) {
                if (remap) {
                    if (parent_raw < 0 || static_cast<std::size_t>(parent_raw) >= bc) {
                        ++*invalid;
                    } else {
                        parent_id = order[static_cast<std::size_t>(parent_raw)];
                    }
                } else {
                    parent_id = parent_raw;
                }
            }
            if (parent_id >= static_cast<int>(bc)) ++*invalid;
            (*map)[static_cast<std::size_t>(bone_index)] = parent_id;
        }
    };

    std::vector<int> direct, remapped;
    int direct_invalid = 0, remapped_invalid = 0;
    build_parent_map(false, &direct, &direct_invalid);
    build_parent_map(true, &remapped, &remapped_invalid);
    const auto& parents = (direct_invalid > 0 && remapped_invalid == 0) ? remapped : direct;

    std::vector<BoneState> bones(bc);
    for (std::size_t i = 0; i < bc; ++i) {
        bones[i].parent = parents[i];
        if (!read_vec3(reader, trans_off + i * 0x20u, &bones[i].local)) return false;
    }
    for (std::size_t i = 0; i < bc; ++i) {
        (void)bone_world(i, &bones);
    }

    for (std::size_t i = 0; i < bc; ++i) {
        const int object_index = child[i];
        if (object_index >= 0 && static_cast<std::size_t>(object_index) < object_count) {
            (*translations)[static_cast<std::size_t>(object_index)] = bones[i].world;
        }
    }
    return true;
}

[[nodiscard]] bool append_mesh(const BinaryReader& reader,
                               Format format,
                               const MeshRec& rec,
                               Vec3 translation,
                               Mesh* output) {
    if (output == nullptr) return false;
    const std::size_t vc = rec.vertex_count;
    if (vc == 0) return true;
    if (output->vertices.size() > kMaxVertices - vc) return false;

    const std::size_t base_vertex = output->vertices.size();
    std::vector<Vec3> local_positions(vc);
    std::vector<Vec3> normals(vc);
    std::vector<bool> skip(vc, false);

    for (std::size_t v = 0; v < vc; ++v) {
        if (!read_vec3(reader, rec.positions + v * 12u, &local_positions[v]) ||
            !read_vec3(reader, rec.normals + v * 12u, &normals[v])) {
            return false;
        }
    }

    if (format == Format::Scm) {
        for (std::size_t v = 0; v < vc; ++v) {
            const auto* flags = reader.ptr(rec.colors + v * 4u + 3u, 1);
            if (flags == nullptr) return false;
            skip[v] = ((*flags & 0x02u) != 0u);
        }
    } else {
        for (std::size_t v = 0; v < vc; ++v) {
            std::uint16_t packed = 0;
            if (!reader.read_le(rec.aux1 + v * 2u, &packed)) return false;
            skip[v] = ((packed & 0x8000u) != 0u);
        }
    }

    output->vertices.reserve(output->vertices.size() + vc);
    for (const auto& p : local_positions) {
        output->vertices.push_back(add(p, translation));
    }

    if (vc < 3) return true;
    if (output->indices.size() > kMaxIndices - (vc - 2u) * 3u) return false;
    output->indices.reserve(output->indices.size() + (vc - 2u) * 3u);

    std::size_t p1 = 0, p2 = 1;
