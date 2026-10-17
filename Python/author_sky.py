"""Authors MobSky's materials, their instances and the noise they march through.

Run from the Sky menu in the level editor toolbar, or by hand with both MobSky/Python and
MobLights/Python on sys.path:

    import importlib, author_sky
    importlib.reload(author_sky)
    author_sky.build_all()

Every phase is idempotent: an existing material is emptied and rebuilt in place, so placed components
keep their references.

The maths lives in Shaders/Public/MobCloud.ush, MobSky.ush and MobSkySpace.ush. Each master is one
Custom node fed by the parameters the components write, and the parameter names have to agree with
MobSkyParams in MobCloudTypes.cpp.

The graph helpers are MobLights', borrowed rather than copied, as author_fog.py borrows them.
"""

import unreal

from author_lights import (
    MEL, EAL, CMOT, MP,
    _tools, _clear_material, _existing, _expr, _spread, _switch, _static_bool,
    _const3, _mul, _sub, _transform_position, _transform_vector, custom, link,
)
from author_fog import (
    _mask, _const, _texture_object, _collection_param,
    COLLECTION_PATH as FOG_COLLECTION_PATH,
)

ROOT = '/MobSky'
MAT_ROOT = ROOT + '/Materials'
TEX_ROOT = ROOT + '/Textures'

SHAPE_PATH = TEX_ROOT + '/T_MobCloudShape'
DETAIL_PATH = TEX_ROOT + '/T_MobCloudDetail'
WEATHER_PATH = TEX_ROOT + '/T_MobCloudWeather'
COLOR_TARGET_PATH = TEX_ROOT + '/RT_MobCloudColor'
DEPTH_TARGET_PATH = TEX_ROOT + '/RT_MobCloudDepth'
COLLECTION_NAME = 'MPC_MobSky'
COLLECTION_PATH = MAT_ROOT + '/' + COLLECTION_NAME

# What the sky tells the clouds. The subsystem writes these; additive only, like MobLights' collection.
COLLECTION_VECTORS = [
    # The horizon's lit colour in rgb, and how much air is above the camera in a.
    ('HazeColor', (0.6, 0.75, 0.95, 1.0)),

    # The deck the sky paints past the layer's reach: weather scale, coverage, planet weather scale
    # and amount; then wind, where the painted deck takes over, and the deck's height.
    ('FarCloudsA', (3000000.0, 0.0, 150000000.0, 0.6)),
    ('FarCloudsB', (800.0, 250.0, 6000000.0, 300000.0)),
]
DEFAULT_CUBE_PATH = '/Engine/EngineResources/DefaultTextureCube'

CLOUD_INCLUDES = ['/MobSky/Public/MobCloud.ush']
SKY_INCLUDES = ['/MobSky/Public/MobSky.ush']


def _log(msg):
    unreal.log('[MobSky] ' + str(msg))


def save(asset):
    EAL.save_loaded_asset(asset, only_if_is_dirty=False)


def _get_or_create_material(name):
    path = MAT_ROOT + '/' + name
    mat = _existing(path)
    if mat is not None:
        _clear_material(mat)
    else:
        mat = _tools().create_asset(name, MAT_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    return mat


def _vector(mat, name, default, group, x, y, description=''):
    e = _expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', unreal.LinearColor(*default))
    e.set_editor_property('group', group)
    e.set_editor_property('desc', description)
    return e


def _one_zero(mat, flag, x, y):
    """A static switch as a literal 1 or 0, so the shader sees a constant and folds the branch away."""
    return _switch(mat, flag, _const(mat, 1.0, x, y), '', _const(mat, 0.0, x, y + 40), '', x + 100, y)


def _view_inputs(mat):
    """Camera relative position of the pixel on the proxy, of the surface behind it, and the camera itself."""
    world_pos = _expr(mat, unreal.MaterialExpressionWorldPosition, 0, 0)
    cam_pos = _expr(mat, unreal.MaterialExpressionCameraPositionWS, 0, 120)
    pixel_rel = _sub(mat, world_pos, '', cam_pos, '', 100, 0)

    scene_depth = _expr(mat, unreal.MaterialExpressionSceneDepth, 0, 240)
    pixel_depth = _expr(mat, unreal.MaterialExpressionPixelDepth, 0, 320)
    depth_ratio = _expr(mat, unreal.MaterialExpressionDivide, 100, 240)
    link(scene_depth, '', depth_ratio, 'A')
    link(pixel_depth, '', depth_ratio, 'B')
    surface_rel = _mul(mat, pixel_rel, '', depth_ratio, '', 200, 0)

    cam_world = _mask(mat, cam_pos, 'rgb', 100, 800)
    screen_uv = _expr(mat, unreal.MaterialExpressionScreenPosition, 0, 900)
    return pixel_rel, surface_rel, cam_pos, cam_world, screen_uv


# ---------------------------------------------------------------------------
# Noise
# ---------------------------------------------------------------------------

def build_noise():
    library = getattr(unreal, 'MobSkyEditorLibrary', None)
    if library is None:
        raise RuntimeError('MobSkyEditorLibrary is missing. Is the MobSkyEditor module built?')
    if not library.build_noise_textures():
        raise RuntimeError('noise textures were not saved')
    _log('noise: %s, %s' % (SHAPE_PATH, DETAIL_PATH))


# ---------------------------------------------------------------------------
# Collection
# ---------------------------------------------------------------------------

def build_parameter_collection():
    """Adds any missing parameter and touches nothing that is there, so materials keep their GUIDs."""
    mpc = _existing(COLLECTION_PATH)
    if mpc is None:
        mpc = _tools().create_asset(COLLECTION_NAME, MAT_ROOT, unreal.MaterialParameterCollection,
                                    unreal.MaterialParameterCollectionFactoryNew())

    vectors = list(mpc.get_editor_property('vector_parameters'))
    present = {str(p.get_editor_property('parameter_name')) for p in vectors}
    added = False
    for name, default in COLLECTION_VECTORS:
        if name in present:
            continue
        param = unreal.CollectionVectorParameter()
        param.set_editor_property('parameter_name', name)
        param.set_editor_property('default_value', unreal.LinearColor(*default))
        vectors.append(param)
        added = True

    if added:
        mpc.set_editor_property('vector_parameters', vectors)
        save(mpc)
    _log('%s: %d vector(s)' % (COLLECTION_NAME, len(vectors)))
    return mpc


# ---------------------------------------------------------------------------
# M_MobCloud
# ---------------------------------------------------------------------------

_CODE_CLOUD = """
FMobCloud Cloud;
Cloud.Base = A.x;
Cloud.Top = A.y;
Cloud.Density = A.z;
Cloud.Coverage = A.w;
Cloud.CoverageScale = B.x;
Cloud.ShapeScale = B.y;
Cloud.DetailScale = B.z;
Cloud.DetailAmount = B.w;
Cloud.Wind = C.xy;
Cloud.BottomFade = C.z;
Cloud.TopFade = C.w;
Cloud.MaxDistance = D.x;
Cloud.ReachPerHeight = D.y;
Cloud.PlanetRadius = D.w;
Cloud.PhaseForward = E.x;
Cloud.PhaseBack = E.y;
Cloud.PhaseBlend = E.z;
Cloud.AmbientBottom = E.w;
Cloud.ShadowDistance = F.x;
Cloud.EdgeFade = F.y;
Cloud.HazeDistance = F.z;
Cloud.Powder = F.w;
Cloud.HazeColor = Haze.rgb;
Cloud.Air = Haze.a;
Cloud.PixelAngle = 2.0f / (View.ViewSizeAndInvSize.y * View.ViewToClip[1][1]);
Cloud.MacroScale = G.x;
Cloud.MacroAmount = G.y;
Cloud.Shear = G.z;
Cloud.Variety = G.w;
Cloud.Time = TimeValue;
Cloud.CamWorld = CamWorld;
Cloud.KeyDir = normalize(KeyDir + float3(0.0f, 0.0f, 1e-5f));
Cloud.KeyColor = KeyColor;
Cloud.Ambient = Ambient;
Cloud.ScatterColor = Scatter;
Cloud.EmissiveColor = Emissive;
Cloud.UseDetail = UseDetail;
Cloud.UseShadow = UseShadow;
Cloud.IsVolume = IsVolume;

const float2 Pixel = floor(ScreenUV * View.ViewSizeAndInvSize.xy);

float2 Depths;
const float4 Result = MobCloudMarch(PixelPos, SurfacePos, CentrePos, AxisX, AxisY, AxisZ, ExtentValue,
                     MobCloudDither(Pixel), (int)(D.z + 0.5f), Cloud,
                     ShapeTex, ShapeTexSampler, DetailTex, DetailTexSampler,
                     WeatherTex, WeatherTexSampler, Depths);

return MobCloudQuadAverage(Result, Pixel);
"""

CLOUD_INPUTS = ['PixelPos', 'SurfacePos', 'CentrePos', 'AxisX', 'AxisY', 'AxisZ', 'ExtentValue',
                'ScreenUV', 'CamWorld', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'Scatter', 'Emissive',
                'KeyDir', 'KeyColor', 'Ambient', 'TimeValue', 'Haze',
                'UseDetail', 'UseShadow', 'IsVolume', 'ShapeTex', 'DetailTex', 'WeatherTex']


def build_cloud_material():
    collection = unreal.load_asset(FOG_COLLECTION_PATH)
    if collection is None:
        raise RuntimeError('MPC_MobFog is missing. Run Light -> Generate Fog Materials first.')
    sky_collection = unreal.load_asset(COLLECTION_PATH)

    mat = _get_or_create_material('M_MobCloud')
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)

    # Premultiplied, because the march returns exactly that, as the fog's does.
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ALPHA_COMPOSITE)
    mat.set_editor_property('two_sided', False)

    # The proxy is inside out and the march clamps to scene depth itself, as the fog's does.
    mat.set_editor_property('disable_depth_test', True)

    # Height fog would wash the cloud a second time with air the cloud is already standing in.
    mat.set_editor_property('use_translucency_vertex_fog', False)

    pixel_rel, surface_rel, cam_pos, cam_world, screen_uv = _view_inputs(mat)

    origin = _const3(mat, (0, 0, 0), 0, 440)
    centre_ws = _transform_position(
        mat, unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL,
        unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD, origin, 100, 440)
    centre_rel = _sub(mat, centre_ws, '', cam_pos, '', 200, 440)

    axes = []
    for index, vec in enumerate([(1, 0, 0), (0, 1, 0), (0, 0, 1)]):
        basis = _const3(mat, vec, 0, 560 + index * 80)
        axes.append(_transform_vector(
            mat, unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
            unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD, basis, 100, 560 + index * 80))

    group = '00 - Cloud'
    params = {
        'A': _vector(mat, 'CloudA', (180000, 420000, 0.0003, 0.45), group, 0, 1000,
                     'Base, top, density, coverage.'),
        'B': _vector(mat, 'CloudB', (3000000, 150000, 25000, 0.4), group, 0, 1060,
                     'Weather scale, shape scale, detail scale, detail amount.'),
        'C': _vector(mat, 'CloudC', (800, 250, 0.05, 0.5), group, 0, 1120,
                     'Wind x, wind y, bottom fade, top fade.'),
        'D': _vector(mat, 'CloudD', (6000000, 3, 48, 637100000), group, 0, 1180,
                     'Max distance, reach per height, step count, planet radius.'),
        'E': _vector(mat, 'CloudE', (0.6, -0.25, 0.35, 0.3), group, 0, 1240,
                     'Phase forward, phase back, phase blend, ambient bottom.'),
        'F': _vector(mat, 'CloudF', (200000, 0.4, 4000000, 0.6), group, 0, 1300,
                     'Shadow distance, edge fade, haze distance, powder.'),
        'G': _vector(mat, 'CloudG', (60000000, 0.8, 0.3, 0.6), group, 0, 1330,
                     'Macro weather scale, macro weather amount, shear, variety.'),
    }
    scatter = _mask(mat, _vector(mat, 'ScatterColor', (1, 1, 1, 1), group, 0, 1360), 'rgb', 100, 1360)
    emissive = _mask(mat, _vector(mat, 'EmissiveColor', (0, 0, 0, 1), group, 0, 1420), 'rgb', 100, 1420)
    extent = _mask(mat, _vector(mat, 'Extent', (1, 1, 1, 0), group, 0, 1480), 'rgb', 100, 1480)

    key_dir = _mask(mat, _collection_param(mat, collection, 'KeyDirection', 0, 1600), 'rgb', 100, 1600)
    key_color = _mask(mat, _collection_param(mat, collection, 'KeyColor', 0, 1660), 'rgb', 100, 1660)
    ambient = _mask(mat, _collection_param(mat, collection, 'AmbientColor', 0, 1720), 'rgb', 100, 1720)
    fog_time = _collection_param(mat, collection, 'FogTime', 0, 1780)
    haze = _collection_param(mat, sky_collection, 'HazeColor', 0, 1840)

    group_tex = '10 - Noise'
    shape_tex = _texture_object(mat, 'ShapeTexture', SHAPE_PATH, group_tex, 0, 1900,
                                'Perlin-Worley in red, Worley octaves in the rest.')
    detail_tex = _texture_object(mat, 'DetailTexture', DETAIL_PATH, group_tex, 0, 1960,
                                 'Worley octaves that erode the edges.')
    weather_tex = _texture_object(mat, 'WeatherTexture', WEATHER_PATH, group_tex, 0, 2020,
                                  'Red is cloud cells, green how tall each grows, blue broad regions.')

    group_features = '20 - Features'
    b_detail = _static_bool(mat, 'bDetail', False, group_features, 0, 2100, 0, 'Erodes the edges into wisps.')
    b_shadow = _static_bool(mat, 'bShadow', False, group_features, 0, 2160, 1,
                            'Each step looks towards the sun.')
    b_volume = _static_bool(mat, 'bVolume', False, group_features, 0, 2220, 2,
                            'An ellipsoid that stays put, rather than a layer that follows the camera.')

    march = custom(mat, _CODE_CLOUD, CMOT.CMOT_FLOAT4, CLOUD_INPUTS, [], 800, 0,
                   'The cloud along the view ray, premultiplied.')
    march.set_editor_property('include_file_paths', CLOUD_INCLUDES)

    wiring = {
        'PixelPos': pixel_rel, 'SurfacePos': surface_rel, 'CentrePos': centre_rel,
        'AxisX': axes[0], 'AxisY': axes[1], 'AxisZ': axes[2], 'ExtentValue': extent,
        'ScreenUV': screen_uv, 'CamWorld': cam_world,
        'Scatter': scatter, 'Emissive': emissive,
        'KeyDir': key_dir, 'KeyColor': key_color, 'Ambient': ambient, 'TimeValue': fog_time,
        'Haze': haze,
        'UseDetail': _one_zero(mat, b_detail, 400, 2100),
        'UseShadow': _one_zero(mat, b_shadow, 400, 2160),
        'IsVolume': _one_zero(mat, b_volume, 400, 2220),
        'ShapeTex': shape_tex, 'DetailTex': detail_tex, 'WeatherTex': weather_tex,
    }
    for name, expr in wiring.items():
        link(expr, '', march, name)

    # A vector parameter's unnamed output is RGB, so the packed ones are wired through RGBA.
    for name, expr in params.items():
        link(expr, 'RGBA', march, name)

    MEL.connect_material_property(_mask(mat, march, 'rgb', 900, 0), '', MP.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(_mask(mat, march, 'a', 900, 200), '', MP.MP_OPACITY)

    _spread(MEL.get_material_expressions(mat))
    MEL.recompile_material(mat)
    save(mat)
    _log('M_MobCloud: %d nodes' % len(MEL.get_material_expressions(mat)))
    return mat


# ---------------------------------------------------------------------------
# Render pass targets and M_MobCloudComposite
# ---------------------------------------------------------------------------

def build_render_targets():
    """Where the render pass leaves its result. Assets rather than runtime targets, so the composite can name them.

    The pass resizes them to the view at runtime; the size saved here only has to exist.
    """
    for path, fmt in ((COLOR_TARGET_PATH, unreal.TextureRenderTargetFormat.RTF_RGBA16F),
                      (DEPTH_TARGET_PATH, unreal.TextureRenderTargetFormat.RTF_RG16F)):
        target = _existing(path)
        if target is None:
            package, _, name = path.rpartition('/')
            target = _tools().create_asset(name, package, unreal.TextureRenderTarget2D,
                                           unreal.TextureRenderTargetFactoryNew())
        target.set_editor_property('size_x', 960)
        target.set_editor_property('size_y', 540)
        target.set_editor_property('render_target_format', fmt)
        target.set_editor_property('clear_color', unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        target.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
        target.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
        save(target)
    _log('targets: %s, %s' % (COLOR_TARGET_PATH, DEPTH_TARGET_PATH))


# The pass stores depth in tens of metres, to fit half floats; MOB_CLOUD_DEPTH_SCALE in MobCloudPass.usf.
_CODE_COMPOSITE = """
const float4 Cloud = ColorTex.SampleLevel(ColorTexSampler, ScreenUV, 0.0f);
const float2 Depths = DepthTex.SampleLevel(DepthTexSampler, ScreenUV, 0.0f).rg * 1000.0f;

// Only the cloud in front of the surface covers it, so a ship inside a cloud is covered by the part
// of it between the ship and the camera.
const float Scene = length(SurfacePos);
const float Cover = Depths.y > 0.0f ? saturate((Scene - Depths.x) / max(Depths.y - Depths.x, 1.0f)) : 1.0f;
return Cloud * Cover;
"""


def build_composite_material():
    mat = _get_or_create_material('M_MobCloudComposite')
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ALPHA_COMPOSITE)
    mat.set_editor_property('two_sided', False)
    mat.set_editor_property('disable_depth_test', True)
    mat.set_editor_property('use_translucency_vertex_fog', False)

    _, surface_rel, _, _, screen_uv = _view_inputs(mat)

    color_tex = _texture_object(mat, 'CloudColor', COLOR_TARGET_PATH, '00 - Pass', 0, 1000,
                                'Inscatter and coverage, from the render pass.')
    depth_tex = _texture_object(mat, 'CloudDepth', DEPTH_TARGET_PATH, '00 - Pass', 0, 1060,
                                'Where the cloud starts and where it has done most of its covering.')

    node = custom(mat, _CODE_COMPOSITE, CMOT.CMOT_FLOAT4, ['ScreenUV', 'SurfacePos', 'ColorTex', 'DepthTex'],
                  [], 800, 0, 'The render pass result, put in front of or behind the scene.')
    node.set_editor_property('include_file_paths', [])
    for name, expr in {'ScreenUV': screen_uv, 'SurfacePos': surface_rel,
                       'ColorTex': color_tex, 'DepthTex': depth_tex}.items():
        link(expr, '', node, name)

    MEL.connect_material_property(_mask(mat, node, 'rgb', 900, 0), '', MP.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(_mask(mat, node, 'a', 900, 200), '', MP.MP_OPACITY)

    _spread(MEL.get_material_expressions(mat))
    MEL.recompile_material(mat)
    save(mat)
    _log('M_MobCloudComposite: %d nodes' % len(MEL.get_material_expressions(mat)))
    return mat


# ---------------------------------------------------------------------------
# M_MobSky
# ---------------------------------------------------------------------------

_CODE_SKY = """
const float3 D = normalize(PixelPos);

// Space turns round the vertical; the air and the sun do not.
const float Cy = cos(State.w);
const float Sy = sin(State.w);
const float3 S = float3(D.x * Cy + D.y * Sy, -D.x * Sy + D.y * Cy, D.z);

const float Jitter = MobSkyHash4(uint3((uint2)(ScreenUV * View.ViewSizeAndInvSize.xy), 7u)).x;

float3 Space = 0.0f;
if (UseBaked > 0.5f)
{
    Space = SpaceTex.SampleLevel(SpaceTexSampler, D, 0.0f).rgb;
}
else
{
    FMobSkyGalaxy Galaxy;
    Galaxy.AxisA = normalize(GalAxisA);
    Galaxy.AxisB = normalize(GalAxisB);
    Galaxy.Normal = normalize(GalNormal);
    Galaxy.Brightness = GalB.x;
    Galaxy.Dust = GalB.y;
    Galaxy.ArmCount = GalB.z;
    Galaxy.ArmTightness = GalB.w;
    Galaxy.Bulge = GalC.x;
    Galaxy.Steps = (int)(GalC.y + 0.5f);
    Galaxy.DiscColor = GalDisc;
    Galaxy.BulgeColor = GalBulge;

    const float3 Far = MobSkyGalaxy(S, Galaxy, Jitter, NoiseTex, NoiseTexSampler)
        + MobSkyDistantGalaxies(S, Distant.x, Distant.y, Distant.z, Distant.w);

    FMobSkyNebula N0;
    N0.Placement = Neb0P; N0.ColorA = Neb0A; N0.ColorB = Neb0B; N0.Params = Neb0X;
    FMobSkyNebula N1;
    N1.Placement = Neb1P; N1.ColorA = Neb1A; N1.ColorB = Neb1B; N1.Params = Neb1X;

    const float4 A0 = MobSkyNebula(S, N0, Jitter, NoiseTex, NoiseTexSampler);
    const float4 A1 = MobSkyNebula(S, N1, Jitter, NoiseTex, NoiseTexSampler);
    Space = Far * A0.a * A1.a + A0.rgb * A1.a + A1.rgb;
}

if (SpaceOnly > 0.5f)
{
    return Space;
}

const float PixelAngle = length(fwidth(D));
const float Twinkle = StarsBV.z * State.x;
const float3 Stars = (MobSkyStarLayer(S, StarsAV.x, StarsAV.y, StarsBV.y, PixelAngle, Twinkle, TimeValue, 1u)
    + 2.0f * MobSkyStarLayer(S, StarsAV.z, StarsAV.w, StarsBV.y, PixelAngle, Twinkle, TimeValue, 2u))
    * StarsBV.x;

FMobSkyAtmosphere Atmos;
Atmos.Zenith = Zenith;
Atmos.Horizon = Horizon;
Atmos.Sunset = Sunset;
Atmos.Night = Night;
Atmos.Ground = Ground;
Atmos.HorizonExponent = AtmosV.x;
Atmos.SunCos = SunV.x;
Atmos.SunIntensity = SunV.y;
Atmos.HaloExponent = SunV.z;
Atmos.HaloIntensity = SunV.w;
Atmos.LimbWidth = AtmosV.y;
Atmos.LimbIntensity = AtmosV.z;
Atmos.Air = State.x;
Atmos.Altitude = State.y;
Atmos.PlanetRadius = State.z;
Atmos.KeyDir = normalize(KeyDir + float3(0.0f, 0.0f, 1e-5f));
Atmos.KeyColor = KeyColor;

const float4 FarCloud = MobSkyFarClouds(D, CamPos, Atmos, FarA, FarB, TimeValue, PixelAngle,
    WeatherTex, WeatherTexSampler);

return MobSkyColor(D, Atmos, Space, Stars, FarCloud);
"""

SKY_VECTORS = [
    # input, parameter, default, rgb only
    ('Zenith', 'SkyZenith', (0.12, 0.3, 0.8, 1), True),
    ('Horizon', 'SkyHorizon', (0.6, 0.75, 0.95, 1), True),
    ('Sunset', 'SkySunset', (1.0, 0.42, 0.15, 1), True),
    ('Night', 'SkyNight', (0.004, 0.006, 0.015, 1), True),
    ('Ground', 'SkyGround', (0.05, 0.08, 0.12, 1), True),
    ('SunV', 'SunA', (0.99996, 20, 250, 0.8), False),
    ('AtmosV', 'AtmosA', (4, 0.012, 1.2, 0), False),
    ('State', 'SkyState', (1, 0, 637100000, 0), False),
    ('StarsAV', 'StarsA', (700, 0.2, 140, 0.25), False),
    ('StarsBV', 'StarsB', (1, 16, 0.35, 0), False),
    ('GalAxisA', 'GalaxyAxisA', (0.87, 0.5, 0, 0), True),
    ('GalAxisB', 'GalaxyAxisB', (-0.25, 0.43, 0.87, 0), True),
    ('GalNormal', 'GalaxyNormal', (0.43, -0.75, 0.5, 0), True),
    ('GalB', 'GalaxyB', (10, 1, 2, 0.3), False),
    ('GalC', 'GalaxyC', (2.5, 24, 0, 0), False),
    ('GalDisc', 'GalaxyDiscColor', (0.75, 0.82, 1, 1), True),
    ('GalBulge', 'GalaxyBulgeColor', (1, 0.8, 0.55, 1), True),
    ('Distant', 'DistantA', (6, 0.35, 1.5, 0.021), False),
    ('Neb0P', 'Nebula0Placement', (0.94, 0, 0.34, 0.21), False),
    ('Neb0A', 'Nebula0ColorA', (0.9, 0.2, 0.45, 1), True),
    ('Neb0B', 'Nebula0ColorB', (0.2, 0.5, 1, 1), True),
    ('Neb0X', 'Nebula0Params', (1.5, 0.8, 0.8, 0), False),
    ('Neb1P', 'Nebula1Placement', (-0.74, 0.62, -0.26, 0.14), False),
    ('Neb1A', 'Nebula1ColorA', (0.2, 0.9, 0.6, 1), True),
    ('Neb1B', 'Nebula1ColorB', (0.9, 0.6, 0.2, 1), True),
    ('Neb1X', 'Nebula1Params', (1, 0.8, 0.8, 17), False),
]

SKY_INPUTS = ['PixelPos', 'ScreenUV', 'KeyDir', 'KeyColor', 'TimeValue', 'UseBaked', 'SpaceOnly',
              'NoiseTex', 'SpaceTex', 'CamPos', 'WeatherTex', 'FarA', 'FarB'] + [name for name, _, _, _ in SKY_VECTORS]


def build_sky_material():
    collection = unreal.load_asset(FOG_COLLECTION_PATH)
    if collection is None:
        raise RuntimeError('MPC_MobFog is missing. Run Light -> Generate Fog Materials first.')

    mat = _get_or_create_material('M_MobSky')
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)

    # Translucent at full opacity, depth tested against a dome larger than anything in view: drawn only
    # where nothing else is, and after the opaque pass rather than competing with it. Opaque would
    # take the height fog, which would bury the sky in fog colour at the ground.
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('two_sided', False)
    mat.set_editor_property('use_translucency_vertex_fog', False)

    pixel_rel, _, _, cam_world, screen_uv = _view_inputs(mat)
    sky_collection = unreal.load_asset(COLLECTION_PATH)
    far_a = _collection_param(mat, sky_collection, 'FarCloudsA', 0, 1840)
    far_b = _collection_param(mat, sky_collection, 'FarCloudsB', 0, 1860)
    weather_tex = _texture_object(mat, 'WeatherTexture', WEATHER_PATH, '10 - Space', 0, 2020,
                                  'The clouds\' weather map, for the deck painted past the layer\'s reach.')

    key_dir = _mask(mat, _collection_param(mat, collection, 'KeyDirection', 0, 1600), 'rgb', 100, 1600)
    key_color = _mask(mat, _collection_param(mat, collection, 'KeyColor', 0, 1660), 'rgb', 100, 1660)
    fog_time = _collection_param(mat, collection, 'FogTime', 0, 1780)

    noise_tex = _texture_object(mat, 'NoiseTexture', SHAPE_PATH, '10 - Space', 0, 1900,
                                'The cloud shape noise, which the galaxy and nebulae break up with.')
    space_tex = _expr(mat, unreal.MaterialExpressionTextureObjectParameter, 0, 1960)
    space_tex.set_editor_property('parameter_name', 'SpaceCube')
    space_tex.set_editor_property('texture', unreal.load_asset(DEFAULT_CUBE_PATH))
    space_tex.set_editor_property('group', '10 - Space')
    space_tex.set_editor_property('desc', 'The baked galaxy and nebulae. Set at runtime by the sky component.')

    b_baked = _static_bool(mat, 'bBaked', False, '20 - Features', 0, 2100, 0,
                           'Reads space from the baked cube instead of marching it.')
    b_space = _static_bool(mat, 'bSpaceOnly', False, '20 - Features', 0, 2160, 1,
                           'Space alone, with no stars and no air. What the bake captures.')

    node = custom(mat, _CODE_SKY, CMOT.CMOT_FLOAT3, SKY_INPUTS, [], 800, 0,
                  'The sky: air, planet, stars, galaxy and nebulae.')
    node.set_editor_property('include_file_paths', SKY_INCLUDES)

    wiring = {
        'PixelPos': pixel_rel, 'ScreenUV': screen_uv,
        'KeyDir': key_dir, 'KeyColor': key_color, 'TimeValue': fog_time,
        'UseBaked': _one_zero(mat, b_baked, 400, 2100),
        'SpaceOnly': _one_zero(mat, b_space, 400, 2160),
        'NoiseTex': noise_tex, 'SpaceTex': space_tex,
        'CamPos': cam_world, 'WeatherTex': weather_tex, 'FarA': far_a, 'FarB': far_b,
    }
    for name, expr in wiring.items():
        link(expr, '', node, name)

    group = '00 - Sky'
    for index, (name, param, default, rgb) in enumerate(SKY_VECTORS):
        expr = _vector(mat, param, default, group, 0, 1000 + index * 40)
        link(expr, '' if rgb else 'RGBA', node, name)

    MEL.connect_material_property(node, '', MP.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(_const(mat, 1.0, 900, 200), '', MP.MP_OPACITY)

    _spread(MEL.get_material_expressions(mat))
    MEL.recompile_material(mat)
    save(mat)
    _log('M_MobSky: %d nodes' % len(MEL.get_material_expressions(mat)))
    return mat


# ---------------------------------------------------------------------------
# Instances
# ---------------------------------------------------------------------------

def _instance(name, master, wanted):
    path = '%s/%s' % (MAT_ROOT, name)
    mi = _existing(path)
    if mi is None:
        mi = _tools().create_asset(name, MAT_ROOT, unreal.MaterialInstanceConstant,
                                   unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', master)

    # Every switch set explicitly, so a rebuild never leaves a stale one on.
    for key in [str(n) for n in MEL.get_static_switch_parameter_names(master)]:
        MEL.set_material_instance_static_switch_parameter_value(mi, key, key in wanted)

    MEL.update_material_instance(mi)
    save(mi)
    return name


def build_instances():
    cloud = unreal.load_asset(MAT_ROOT + '/M_MobCloud')
    sky = unreal.load_asset(MAT_ROOT + '/M_MobSky')

    # The names and the bit order have to match MobCloudVariant::InstanceName.
    built = []
    for variant in range(8):
        wanted = set()
        name = 'MI_MobCloud_' + ('Volume' if variant & 4 else 'Layer')
        if variant & 4:
            wanted.add('bVolume')
        if variant & 1:
            wanted.add('bDetail')
            name += '_Detail'
        if variant & 2:
            wanted.add('bShadow')
            name += '_Shadow'
        built.append(_instance(name, cloud, wanted))

    built.append(_instance('MI_MobSky', sky, set()))
    built.append(_instance('MI_MobSky_Baked', sky, {'bBaked'}))
    built.append(_instance('MI_MobSky_Space', sky, {'bSpaceOnly'}))

    _log('instances: %s' % ', '.join(built))
    return built


def build_all():
    build_noise()
    build_parameter_collection()
    build_render_targets()
    build_cloud_material()
    build_composite_material()
    build_sky_material()
    build_instances()
    _log('done')
