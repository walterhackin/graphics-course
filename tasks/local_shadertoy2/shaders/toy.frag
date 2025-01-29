#version 430

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D iChannel0;
layout(binding = 1) uniform sampler2D iChannel1;

layout(push_constant) uniform PushedParams {
    uint  resolution_x;
    uint  resolution_y;
    float time;
    float mouse_x;
    float mouse_y;
} pc;

float iTime;
vec2  iResolution;
vec2  iMouse;

#define MAX_STEPS 100
#define MAX_DIST  100.0
#define EPSILON   0.001


float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

float sdPlane(vec3 p) {
    return p.y;
}

float opUnion(float d1, float d2) {
    return min(d1, d2);
}


float mapScene(vec3 p) {
    float bigSphere = sdSphere(p - vec3(0.0, 1.0, 0.0), 1.0);

    float orbitRadius = 2.0;
    float planetX = orbitRadius * sin(iTime);
    float planetZ = orbitRadius * cos(iTime);
    float smallSphere = sdSphere(p - vec3(planetX, 1.0, planetZ), 0.3);

    float plane = sdPlane(p);

    float distScene = opUnion(bigSphere, smallSphere);
    distScene = opUnion(distScene, plane);
    return distScene;
}


vec3 calcNormal(vec3 p) {
    float d = mapScene(p);
    vec2 e = vec2(0.002, 0.0);
    vec3 n;
    n.x = mapScene(p + vec3(e.x, e.y, e.y)) - d;
    n.y = mapScene(p + vec3(e.y, e.x, e.y)) - d;
    n.z = mapScene(p + vec3(e.y, e.y, e.x)) - d;
    return normalize(n);
}

float rayMarch(vec3 ro, vec3 rd) {
    float t = 0.0;
    for(int i = 0; i < MAX_STEPS; i++) {
        vec3 p = ro + rd * t;
        float dist = mapScene(p);
        if(dist < EPSILON)
            return t;
        t += dist;
        if(t > MAX_DIST) break;
    }
    return -1.0;
}


vec3 triPlanarTexture(vec3 p, vec3 n)
{
    vec3 an = abs(n);
    float sum = an.x + an.y + an.z + 1e-8;

    float scale = 1.0;

    vec2 uvx = p.zy * 0.5 * scale;
    vec2 uvy = p.xz * 0.5 * scale;
    vec2 uvz = p.xy * 0.5 * scale;

    vec3 tx = texture(iChannel1, uvx).rgb;
    vec3 ty = texture(iChannel1, uvy).rgb;
    vec3 tz = texture(iChannel1, uvz).rgb;

    vec3 color = (tx * an.x + ty * an.y + tz * an.z) / sum;
    return color;
}


vec3 lighting(vec3 p, vec3 ro, vec3 rd)
{
    vec3 n = calcNormal(p);

    vec3 baseColor = triPlanarTexture(p, n);

    vec3 lightPos   = vec3(6.0, 5.0, 3.0);
    vec3 lightColor = vec3(1.0, 0.97, 0.9);
    vec3 L = normalize(lightPos - p);

    float diff = max(dot(n, L), 0.0);

    vec3 R = reflect(-L, n);
    float spec = pow(max(dot(R, -rd), 0.0), 32.0);

    float shadow = 1.2;
    {
        float distToLight = rayMarch(p + n * EPSILON * 2.0, L);
        float distLight   = length(lightPos - p);
        if(distToLight > 0.0 && distToLight < distLight)
            shadow = 0.2;
    }

    vec3 ambient = vec3(0.05);

    vec3 color = baseColor * (ambient + lightColor * diff * shadow)
               + lightColor * spec * shadow;

    color += 0.05 * n;
    return color;
}


void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float flippedY = (iResolution.y - 1.0) - fragCoord.y;
    vec2 uv = (vec2(fragCoord.x, flippedY) - 0.5 * iResolution) / iResolution.y;

    vec2 mouse = iMouse / iResolution;
    if (iMouse == vec2(0.0)) {
        mouse = vec2(0.5, 0.5);
    }
    float yaw   = (mouse.x * 2.0 - 1.0) * 3.14159;
    float pitch = (mouse.y - 0.5)       * 3.14159 * 0.5;

    float radius = 5.0;
    vec3 ro = vec3(
        sin(yaw)*cos(pitch)*radius,
        sin(pitch)*radius + 2.0,
        cos(yaw)*cos(pitch)*radius
    );

    vec3 center = vec3(0.0, 1.0, 0.0);
    vec3 cw = normalize(center - ro);
    vec3 cr = normalize(cross(vec3(0,1,0), cw));
    vec3 cu = cross(cw, cr);

    vec3 rd = normalize(uv.x*cr + uv.y*cu + cw);

    float t = rayMarch(ro, rd);

    vec3 skyColorTop = vec3(0.6, 0.8, 1.0);
    vec3 skyColorBot = vec3(0.9, 0.9, 1.0);
    vec3 col = mix(skyColorTop, skyColorBot, uv.y + 0.5);

    if(t > 0.0)
    {
        vec3 p = ro + rd * t;
        col = lighting(p, ro, rd);
    }

    fragColor = vec4(col, 1.0);
}

void main()
{
    ivec2 fragCoord = ivec2(gl_FragCoord.xy);

    iResolution = vec2(pc.resolution_x, pc.resolution_y);
    iTime       = pc.time;
    iMouse      = vec2(pc.mouse_x, pc.mouse_y);

    mainImage(fragColor, vec2(fragCoord));
}