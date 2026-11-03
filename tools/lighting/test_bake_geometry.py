import unittest

from bake_geometry import (face_basis, group_coplanar_faces, newell_normal, pack_lightmap_charts,
                           plane_chart_basis, rle_bgra)


class FaceBasisTests(unittest.TestCase):
    def test_collinear_prefix_retains_visible_rectangle(self):
        points = [(0, 0, 0), (1, 0, 0), (2, 0, 0), (2, 0, 2), (0, 0, 2)]
        axis_u, axis_v = face_basis(points)
        self.assertAlmostEqual(sum(value * value for value in axis_u), 1)
        self.assertAlmostEqual(sum(value * value for value in axis_v), 1)
        self.assertAlmostEqual(sum(u * v for u, v in zip(axis_u, axis_v)), 0)
        # Both axes stay in the vertical face plane, with no collapsed chart dimension.
        self.assertEqual(axis_u[1], 0)
        self.assertEqual(axis_v[1], 0)

    def test_duplicate_prefix_is_not_degenerate(self):
        self.assertIsNotNone(face_basis([(0, 0, 0), (0, 0, 0), (2, 0, 0), (0, 2, 0)]))

    def test_entirely_collinear_face_is_degenerate(self):
        self.assertIsNone(face_basis([(0, 0, 0), (1, 0, 0), (2, 0, 0), (3, 0, 0)]))
        self.assertIsNone(face_basis([(0, 0, 0)]))

    def test_winding_is_preserved(self):
        axis_u, axis_v = face_basis([(0, 0, 0), (2, 0, 0), (0, 2, 0)])
        self.assertGreater(axis_u[0] * axis_v[1] - axis_u[1] * axis_v[0], 0)
        axis_u, axis_v = face_basis([(0, 0, 0), (0, 2, 0), (2, 0, 0)])
        self.assertLess(axis_u[0] * axis_v[1] - axis_u[1] * axis_v[0], 0)


class LightmapChartPackingTests(unittest.TestCase):
    def test_sorts_charts_and_crops_page_dimensions(self):
        placements, dimensions = pack_lightmap_charts([(20, 10), (40, 30), (20, 10)], 64, padding=4)

        self.assertEqual(dimensions, [(56, 56)])
        self.assertEqual(placements[1], (0, 4, 4, 40, 30))
        self.assertEqual(placements[0], (0, 4, 42, 20, 10))
        self.assertEqual(placements[2], (0, 32, 42, 20, 10))

    def test_clamps_oversized_chart_inside_page(self):
        placements, dimensions = pack_lightmap_charts([(100, 80)], 64, padding=4)

        self.assertEqual(placements, [(0, 4, 4, 56, 56)])
        self.assertEqual(dimensions, [(64, 64)])


class LightmapCompressionTests(unittest.TestCase):
    def test_encodes_literal_and_repeated_texels(self):
        a = bytes((1, 2, 3, 4))
        b = bytes((5, 6, 7, 8))
        c = bytes((9, 10, 11, 12))

        self.assertEqual(rle_bgra(a + b + c + c + c + a),
                         bytes((1,)) + a + b + bytes((0x82,)) + c + bytes((0,)) + a)

    def test_splits_spans_at_128_texels(self):
        pixel = bytes((1, 2, 3, 4))
        encoded = rle_bgra(pixel * 129)

        self.assertEqual(encoded, bytes((0xff,)) + pixel + bytes((0,)) + pixel)


class IndoorChartTests(unittest.TestCase):
    def test_floor_and_wall_bases_are_world_aligned(self):
        self.assertEqual(plane_chart_basis((0.0, 0.0, 1.0)), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0)))
        axis_u, axis_v = plane_chart_basis((1.0, 0.0, 0.0))
        self.assertEqual(axis_u, (0.0, 1.0, 0.0))
        self.assertEqual(axis_v, (0.0, 0.0, 1.0))

    def test_newell_normal_follows_winding(self):
        self.assertEqual(newell_normal([(0, 0, 0), (2, 0, 0), (2, 2, 0), (0, 2, 0)]), (0.0, 0.0, 8.0))

    def test_faces_split_at_a_portal_share_a_chart(self):
        up = (0.0, 0.0, 1.0)
        left = [(0, 0, 0), (100, 0, 0), (100, 100, 0), (0, 100, 0)]
        right = [(100, 0, 0), (200, 0, 0), (200, 100, 0), (100, 100, 0)]
        # Back to back with the right one, and a parallel floor one unit higher: neither joins.
        below = [(100, 0, 0), (100, 100, 0), (200, 100, 0), (200, 0, 0)]
        higher = [(200, 0, 1), (300, 0, 1), (300, 100, 1), (200, 100, 1)]
        groups = group_coplanar_faces([(left, up), (right, up), (below, (0.0, 0.0, -1.0)), (higher, up)])
        self.assertEqual(groups, [[0, 1], [2], [3]])

    def test_touching_faces_far_apart_in_l_shape_keep_their_own_charts(self):
        up = (0.0, 0.0, 1.0)
        corridor = [(0, 0, 0), (1000, 0, 0), (1000, 100, 0), (0, 100, 0)]
        branch = [(900, 100, 0), (1000, 100, 0), (1000, 1000, 0), (900, 1000, 0)]
        self.assertEqual(group_coplanar_faces([(corridor, up), (branch, up)]), [[0], [1]])

    def test_first_fit_fills_shelves_before_opening_new_ones(self):
        sizes = [(10, 10)] + [(10, 4)] * 4
        _, best = pack_lightmap_charts(sizes, 64, padding=1)
        placements, first = pack_lightmap_charts(sizes, 64, padding=1, first_fit=True)
        self.assertEqual(first, [(60, 12)])
        self.assertGreater(best[0][1], first[0][1])
        self.assertTrue(all(page == 0 and y == 1 for page, _, y, _, _ in placements))
