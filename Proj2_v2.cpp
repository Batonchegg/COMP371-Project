#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <list>
#include <glm/gtx/norm.hpp>

#define STB_IMAGE_IMPLEMENTATION


const unsigned int SHADOW_WIDTH = 1024, SHADOW_HEIGHT = 1024;
const float SHADOW_NEAR = 0.1f, SHADOW_FAR = 50.0f;


#include <stb/stb_image.h>

// ============= Shaders =============
static const char* kVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 worldMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

void main() {
    vec4 worldPos = worldMatrix * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(worldMatrix))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = projectionMatrix * viewMatrix * worldPos;
}
)";

static const char* kFS = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D texture1;
uniform bool useTexture;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 lightPos2;
uniform vec3 lightColor2;
uniform vec3 objectColor;

uniform samplerCube depthMap1;
uniform samplerCube depthMap2;
uniform float far_plane;

float ShadowCalculation(vec3 fragPos, vec3 lightPos, samplerCube depthMap) {
    vec3 fragToLight = fragPos - lightPos;
    float currentDepth = length(fragToLight);
    float shadow = 0.0;
    float bias = max(0.05 * (1.0 - dot(normalize(Normal), normalize(fragToLight))), 0.005);
    vec3 sampleOffsetDirections[9] = vec3[](
        vec3( 1,  0,  0), vec3(-1,  0,  0), vec3( 0,  1,  0),
        vec3( 0, -1,  0), vec3( 1,  1,  0), vec3(-1,  1,  0),
        vec3( 1, -1,  0), vec3(-1, -1,  0), vec3( 0,  0,  0)
    );
    float sampleRadius = 0.05; // Adjust for shadow softness
    for (int i = 0; i < 9; ++i) {
        vec3 sampleDir = fragToLight + sampleOffsetDirections[i] * sampleRadius;
        float closestDepth = texture(depthMap, sampleDir).r * far_plane;
        shadow += (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
    }
    shadow /= 9.0;
    return shadow;
}


void main() {
    // Phong (two lights)
    float ambientStrength = 0.2;
    vec3 ambient = ambientStrength * lightColor;

    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);

    // Light 1
    vec3 L1 = normalize(lightPos - FragPos);
    float diff1 = max(dot(N, L1), 0.0);
    vec3 R1 = reflect(-L1, N);
    float spec1 = pow(max(dot(V, R1), 0.0), 32.0);
    //vec3 lit1 = diff1 * lightColor + 0.2 * spec1 * lightColor;

    // Light 2
    vec3 L2 = normalize(lightPos2 - FragPos);
    float diff2 = max(dot(N, L2), 0.0);
    vec3 R2 = reflect(-L2, N);
    float spec2 = pow(max(dot(V, R2), 0.0), 32.0);
    //vec3 lit2 = diff2 * lightColor2 + 0.2 * spec2 * lightColor2;

    vec3 baseColor = useTexture ? texture(texture1, TexCoord).rgb : objectColor;
        float shadow1 = ShadowCalculation(FragPos, lightPos, depthMap1);
    float shadow2 = ShadowCalculation(FragPos, lightPos2, depthMap2);
    vec3 lit1 = (1.0 - shadow1) * (diff1 * lightColor + 0.2 * spec1 * lightColor);
    vec3 lit2 = (1.0 - shadow2) * (diff2 * lightColor2 + 0.2 * spec2 * lightColor2);
    vec3 lighting = (ambient + lit1 + lit2) * baseColor;
 
    FragColor = vec4(lighting, 1.0);
}
)";


static const char* depthVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 model;
out vec4 vWorldPos; // name matches GS

void main()
{
    vWorldPos = model * vec4(aPos, 1.0);
    // set gl_Position to something (not used by later pipeline because GS will overwrite for each layer),
    // but it's required to have a defined gl_Position for pipeline consistency
   gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

static const char* depthGS = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 18) out;
uniform mat4 shadowMatrices[6];
in vec4 vWorldPos[];    // matches VS out
out vec4 FragPos;       // passed to depth FS

void main()
{
    for (int face = 0; face < 6; ++face)
    {
        gl_Layer = face;
        for (int i = 0; i < 3; ++i)
        {
            FragPos = vWorldPos[i];
            gl_Position = shadowMatrices[face] * FragPos; // transform into clip space for that face
            EmitVertex();
        }
        EndPrimitive();
    }
}
)";

static const char* depthFS = R"(
#version 330 core
in vec4 FragPos;
uniform vec3 lightPos;
uniform float far_plane;
void main() {
    float lightDistance = length(FragPos.xyz - lightPos);
    lightDistance /= far_plane;
    gl_FragDepth = lightDistance;
}
)";



class Projectile
{
public:
    Projectile(glm::vec3 position, glm::vec3 velocity, int shaderProgram) : mPosition(position), mVelocity(velocity)
    {
        mWorldMatrixLocation = glGetUniformLocation(shaderProgram, "worldMatrix");

    }
    
    void Update(float dt)
    {
        mPosition += mVelocity * dt;
    }
    
    void Draw(GLuint shaderProgram, GLuint VAO, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
        
        glm::mat4 worldMatrix = glm::translate(glm::mat4(1.0f), mPosition) * glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.2f, 0.2f, 0.2f));
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "worldMatrix"), 1, GL_FALSE, &worldMatrix[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "viewMatrix"), 1, GL_FALSE, &viewMatrix[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projectionMatrix"), 1, GL_FALSE, &projectionMatrix[0][0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "objectColor"), 1, glm::value_ptr(glm::vec3(1.0f, 0.0f, 0.0f))); // Red projectile
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 0);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }
    
private:
    GLuint mWorldMatrixLocation;
    glm::vec3 mPosition;
    glm::vec3 mVelocity;
};


// ============= GL utils =============
static GLuint Compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(sh, 1024, nullptr, log); std::cerr << "Shader error:\n" << log << "\n"; }
    return sh;
}
static GLuint Link(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); std::cerr << "Link error:\n" << log << "\n"; }
    return p;
}

// ============= Geometry (Cube / Sphere / Cone) =============
static float cubeVerts[] = {
    // pos                 // normal         // uv
    -0.5f,-0.5f,-0.5f, 0,0,-1, 0,0,  0.5f,-0.5f,-0.5f, 0,0,-1, 1,0,  0.5f,0.5f,-0.5f, 0,0,-1, 1,1,
     0.5f,0.5f,-0.5f, 0,0,-1, 1,1, -0.5f,0.5f,-0.5f, 0,0,-1, 0,1, -0.5f,-0.5f,-0.5f, 0,0,-1, 0,0,

    -0.5f,-0.5f, 0.5f, 0,0,1, 0,0,  0.5f,-0.5f, 0.5f, 0,0,1, 1,0,  0.5f,0.5f, 0.5f, 0,0,1, 1,1,
     0.5f,0.5f, 0.5f, 0,0,1, 1,1, -0.5f,0.5f, 0.5f, 0,0,1, 0,1, -0.5f,-0.5f, 0.5f, 0,0,1, 0,0,

    -0.5f,0.5f,0.5f, -1,0,0, 1,0, -0.5f,0.5f,-0.5f, -1,0,0, 1,1, -0.5f,-0.5f,-0.5f, -1,0,0, 0,1,
    -0.5f,-0.5f,-0.5f, -1,0,0, 0,1, -0.5f,-0.5f,0.5f, -1,0,0, 0,0, -0.5f,0.5f,0.5f, -1,0,0, 1,0,

     0.5f,0.5f,0.5f, 1,0,0, 1,0,  0.5f,0.5f,-0.5f, 1,0,0, 1,1,  0.5f,-0.5f,-0.5f, 1,0,0, 0,1,
     0.5f,-0.5f,-0.5f, 1,0,0, 0,1,  0.5f,-0.5f,0.5f, 1,0,0, 0,0,  0.5f,0.5f,0.5f, 1,0,0, 1,0,

    -0.5f,-0.5f,-0.5f, 0,-1,0, 0,1,  0.5f,-0.5f,-0.5f, 0,-1,0, 1,1,  0.5f,-0.5f,0.5f, 0,-1,0, 1,0,
     0.5f,-0.5f,0.5f, 0,-1,0, 1,0, -0.5f,-0.5f,0.5f, 0,-1,0, 0,0, -0.5f,-0.5f,-0.5f, 0,-1,0, 0,1,

    -0.5f,0.5f,-0.5f, 0,1,0, 0,1,  0.5f,0.5f,-0.5f, 0,1,0, 1,1,  0.5f,0.5f,0.5f, 0,1,0, 1,0,
     0.5f,0.5f,0.5f, 0,1,0, 1,0, -0.5f,0.5f,0.5f, 0,1,0, 0,0, -0.5f,0.5f,-0.5f, 0,1,0, 0,1
};

static void GenSphere(float r, int sectors, int stacks,
                      std::vector<float>& v, std::vector<unsigned int>& idx) {
    const float PI = 3.14159265359f;
    v.clear(); idx.clear();
    for (int i = 0; i <= stacks; ++i) {
        float vv = i / float(stacks);
        float phi = vv * PI;
        for (int j = 0; j <= sectors; ++j) {
            float uu = j / float(sectors);
            float th = uu * 2.0f * PI;
            float x = cosf(th) * sinf(phi);
            float y = cosf(phi);
            float z = sinf(th) * sinf(phi);
            // pos
            v.push_back(x * r); v.push_back(y * r); v.push_back(z * r);
            // normal
            v.push_back(x); v.push_back(y); v.push_back(z);
            // uv
            v.push_back(uu); v.push_back(vv);
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < sectors; ++j) {
            int k1 = i * (sectors + 1) + j;
            int k2 = k1 + sectors + 1;
            idx.push_back(k1); idx.push_back(k2); idx.push_back(k1 + 1);
            idx.push_back(k1 + 1); idx.push_back(k2); idx.push_back(k2 + 1);
        }
    }
}

static void GenCone(float radius, float height, int sectors,
                    std::vector<float>& v, std::vector<unsigned int>& idx) {
    v.clear(); idx.clear();
    const float PI = 3.14159265359f;

    // tip
    glm::vec3 tip = glm::vec3(0.0f, height * 0.5f, 0.0f);
    v.insert(v.end(), { tip.x, tip.y, tip.z, 0, 1, 0, 0, 0 });
    int tipIndex = 0;

    // base center
    glm::vec3 baseC = glm::vec3(0.0f, -height * 0.5f, 0.0f);
    v.insert(v.end(), { baseC.x, baseC.y, baseC.z, 0, -1, 0, 0, 0 });
    int baseCenterIndex = 1;

    // ring
    int ringStart = 2;
    for (int i = 0; i <= sectors; ++i) {
        float a = float(i % sectors) * (2 * PI / sectors);
        float x = radius * cosf(a);
        float z = radius * sinf(a);
        glm::vec3 p = glm::vec3(x, baseC.y, z);
        glm::vec3 t = glm::normalize(glm::vec3(-sinf(a), 0, cosf(a)));
        glm::vec3 s = glm::normalize(glm::vec3(x, height, z));
        glm::vec3 n = glm::normalize(glm::cross(t, s));
        v.insert(v.end(), { p.x, p.y, p.z, n.x, n.y, n.z, (x / radius + 1) * 0.5f, (z / radius + 1) * 0.5f });
    }

    // sides
    for (int i = 0; i < sectors; ++i) {
        int i0 = ringStart + i;
        int i1 = ringStart + i + 1;
        idx.push_back(tipIndex); idx.push_back(i0); idx.push_back(i1);
    }
    // base
    for (int i = 0; i < sectors; ++i) {
        int i0 = ringStart + i + 1;
        int i1 = ringStart + i;
        idx.push_back(baseCenterIndex); idx.push_back(i0); idx.push_back(i1);
    }
}

// ============= Texture =============
static GLuint LoadTexture(const char* file) {
    stbi_set_flip_vertically_on_load(true);
    int w, h, n;
    unsigned char* data = stbi_load(file, &w, &h, &n, 0);
    GLuint tex = 0;
    if (!data) {
        const char* why = stbi_failure_reason();
        std::cerr << "[stb_image] Failed to load '" << file << "': "
                  << (why ? why : "unknown") << "\n";
        // make a magenta fallback so the app still runs
        unsigned char px[3] = {255, 0, 255};
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }
    GLenum fmt = (n == 4) ? GL_RGBA : GL_RGB;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
}

// ============= Drawing helpers =============
static void DrawObject(GLuint prog, GLuint VAO,
                       const glm::mat4& M, const glm::vec3& color,
                       const glm::mat4& V, const glm::mat4& P,
                       GLuint texture = 0, bool indexed = false, GLuint EBO = 0, int count = 0) {
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "worldMatrix"), 1, GL_FALSE, glm::value_ptr(M));
    glUniformMatrix4fv(glGetUniformLocation(prog, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(V));
    glUniformMatrix4fv(glGetUniformLocation(prog, "projectionMatrix"), 1, GL_FALSE, glm::value_ptr(P));
    glUniform3fv(glGetUniformLocation(prog, "objectColor"), 1, glm::value_ptr(color));
    if (texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(prog, "useTexture"), 1);
        glUniform1i(glGetUniformLocation(prog, "texture1"), 0);
    } else {
        glUniform1i(glGetUniformLocation(prog, "useTexture"), 0);
    }
    glBindVertexArray(VAO);
    if (indexed) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glBindVertexArray(0);
    if (texture) glBindTexture(GL_TEXTURE_2D, 0);
}

// ============= Camera / Input State =============
static float lastX = 1200.0f / 2.0f, lastY = 700.0f / 2.0f;
static bool firstMouse = true;
static float yawDeg = -90.0f, pitchDeg = 0.0f;

static glm::vec3 camPos = glm::vec3(0.0f, 0.51f, 1.0f);
static glm::vec3 camFront = glm::vec3(0.0f, 0.0f, -1.0f);
static glm::vec3 camUp = glm::vec3(0.0f, 1.0f, 0.0f);

static float deltaTime = 0.0f, lastFrame = 0.0f;
static bool chaseCam = false;
const float camStrafeSpeed = 6.0f; // units per second


// Zoom (Z/X)
float fovDeg = 70.0f;
const float fovMin = 25.0f;
const float fovMax = 100.0f;

// player
static glm::vec3 playerPos = glm::vec3(0.0f, 1.0f, 0.0f);
static glm::vec3 playerVel = glm::vec3(0.0f);
static bool onGround = false;
static int onPillar = -1;

static const float playerRadiusXZ = 0.45f;
static const float gravityC = 12.0f;
static const float jumpSpeed = 6.0f;
static const float moveSpeed = 5.0f;

// lights
static glm::vec3 lightPos(1.2f, 2.5f, 1.5f);
static glm::vec3 lightPos2(25.2f, 2.5f, 25.5f);
static glm::vec3 lightColor2(1.0f, 0.0f, 0.0f);

// ============= Input callbacks =============
static void MouseCB(GLFWwindow* w, double xpos, double ypos) {
    if (firstMouse) { lastX = float(xpos); lastY = float(ypos); firstMouse = false; }
    float xoff = float(xpos - lastX);
    float yoff = float(lastY - ypos);
    lastX = float(xpos); lastY = float(ypos);
    float sens = 0.5f;
    yawDeg += xoff * sens;
    pitchDeg += yoff * sens;
    if (pitchDeg > 89.0f) pitchDeg = 89.0f;
    if (pitchDeg < -89.0f) pitchDeg = -89.0f;
    glm::vec3 f;
    f.x = cosf(glm::radians(yawDeg)) * cosf(glm::radians(pitchDeg));
    f.y = sinf(glm::radians(pitchDeg));
    f.z = sinf(glm::radians(yawDeg)) * cosf(glm::radians(pitchDeg));
    camFront = glm::normalize(f);
}

static void ProcessWorldInput(GLFWwindow* window, glm::vec3& sunPos) {
    float m = 2.0f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)   sunPos.y += m;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) sunPos.y -= m;
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS)    chaseCam = true;
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS)    chaseCam = false;
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);

    // Z/X zoom
    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) { // zoom OUT (wider)
        fovDeg += 60.0f * deltaTime;
        if (fovDeg > fovMax) fovDeg = fovMax;
    }
    if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) { // zoom IN (narrower)
        fovDeg -= 60.0f * deltaTime;
        if (fovDeg < fovMin) fovDeg = fovMin;
    }
    if (!chaseCam) { // camera movement outside of chasig phase
        glm::vec3 right = glm::normalize(glm::cross(camFront, camUp));
        glm::vec3 forward = glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z)); // keep horizontal
        float s = camStrafeSpeed * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS)
            camPos -= right * s; // left
        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS)
            camPos += right * s; // right
        if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS)
            camPos += forward * s; // forward
        if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS)
            camPos -= forward * s; // backward
    }
}

static void ProcessPlayer(GLFWwindow* window, float dt) {
    glm::vec3 move(0.0f);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) move.x -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) move.x += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) move.z -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) move.z += 1.0f;
    if (glm::length(move) > 0.0f) move = glm::normalize(move);

    playerVel.x = move.x * moveSpeed;
    playerVel.z = move.z * moveSpeed;

    static int lastSpace = GLFW_RELEASE;
    int nowSpace = glfwGetKey(window, GLFW_KEY_SPACE);
    if (nowSpace == GLFW_PRESS && lastSpace == GLFW_RELEASE && onGround) {
        playerVel.y = jumpSpeed;
        onGround = false; onPillar = -1;
    }
    lastSpace = nowSpace;
}

// ============= Character pieces =============
static void DrawObjectBall(GLuint prog, GLuint vao, GLuint ebo, int count,
                           const glm::mat4& parent, const glm::vec3& pos, const glm::vec3& scl,
                           const glm::vec3& color, const glm::mat4& V, const glm::mat4& P) {
    glm::mat4 M = parent * glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), scl);
    DrawObject(prog, vao, M, color, V, P, 0, true, ebo, count);
}
static void DrawObjectCone(GLuint prog, GLuint vao, GLuint ebo, int count,
                           const glm::mat4& parent, const glm::vec3& pos, const glm::vec3& rotAxis, float rotDeg,
                           const glm::vec3& scl, const glm::vec3& color, const glm::mat4& V, const glm::mat4& P) {
    glm::mat4 M = parent *
        glm::translate(glm::mat4(1.0f), pos) *
        glm::rotate(glm::mat4(1.0f), glm::radians(rotDeg), rotAxis) *
        glm::scale(glm::mat4(1.0f), scl);
    DrawObject(prog, vao, M, color, V, P, 0, true, ebo, count);
}

static void DrawCharacter(GLuint prog,
                          GLuint sphereVAO, GLuint sphereEBO, int sphereIdxCount,
                          GLuint coneVAO,   GLuint coneEBO,   int coneIdxCount,
                          const glm::vec3& pos, const glm::mat4& V, const glm::mat4& P,
                          float animT) {
    glm::mat4 root = glm::translate(glm::mat4(1.0f), pos);

    glm::vec3 yellow = glm::vec3(1.0f, 0.95f, 0.2f);
    glm::vec3 purple = glm::vec3(0.63f, 0.26f, 0.96f);
    glm::vec3 green  = glm::vec3(0.2f, 1.0f, 0.2f);
    glm::vec3 pink   = glm::vec3(1.0f, 0.6f, 0.8f);
    glm::vec3 blue   = glm::vec3(0.2f, 0.3f, 1.0f);

    // body stack
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.6f),  yellow, V, P);
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(0.0f, 1.6f, 0.0f), glm::vec3(0.45f), pink,   V, P);
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.35f), yellow, V, P);
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(0.0f, 2.35f, 0.0f), glm::vec3(0.28f), blue,  V, P);

    // hat
    DrawObjectCone(prog, coneVAO, coneEBO, coneIdxCount, root,
                   glm::vec3(0.0f, 2.65f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), -90.0f,
                   glm::vec3(0.25f, 0.6f, 0.25f), purple, V, P);

    // arms
    float swing = sinf(animT * 3.0f) * 20.0f;
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(-0.55f, 1.5f, 0.0f), glm::vec3(0.18f), yellow, V, P);
    DrawObjectCone(prog, coneVAO, coneEBO, coneIdxCount, root,
                   glm::vec3(-0.9f, 1.3f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 90.0f + swing,
                   glm::vec3(0.15f, 0.5f, 0.15f), purple, V, P);
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(-1.2f, 1.1f, 0.0f), glm::vec3(0.14f), blue, V, P);

    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(0.55f, 1.5f, 0.0f), glm::vec3(0.18f), yellow, V, P);
    DrawObjectCone(prog, coneVAO, coneEBO, coneIdxCount, root,
                   glm::vec3(0.9f, 1.3f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), -90.0f - swing,
                   glm::vec3(0.15f, 0.5f, 0.15f), purple, V, P);
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(1.2f, 1.1f, 0.0f), glm::vec3(0.14f), blue, V, P);

    // legs
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(-0.25f, 0.9f, 0.0f), glm::vec3(0.18f), yellow, V, P);
    DrawObjectCone(prog, coneVAO, coneEBO, coneIdxCount, root,
                   glm::vec3(-0.25f, 0.5f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.0f,
                   glm::vec3(0.16f, 0.7f, 0.16f), green, V, P);
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(-0.25f, 0.1f, 0.0f), glm::vec3(0.22f), blue, V, P);

    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(0.25f, 0.9f, 0.0f), glm::vec3(0.18f), yellow, V, P);
    DrawObjectCone(prog, coneVAO, coneEBO, coneIdxCount, root,
                   glm::vec3(0.25f, 0.5f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.0f,
                   glm::vec3(0.16f, 0.7f, 0.16f), green, V, P);
    DrawObjectBall(prog, sphereVAO, sphereEBO, sphereIdxCount, root,
                   glm::vec3(0.25f, 0.1f, 0.0f), glm::vec3(0.22f), blue, V, P);

    // shoulder spikes
    DrawObjectCone(prog, coneVAO, coneEBO, coneIdxCount, root,
                   glm::vec3(-0.55f, 1.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 180.0f,
                   glm::vec3(0.08f, 0.35f, 0.08f), purple, V, P);
    DrawObjectCone(prog, coneVAO, coneEBO, coneIdxCount, root,
                   glm::vec3(0.55f, 1.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 180.0f,
                   glm::vec3(0.08f, 0.35f, 0.08f), purple, V, P);
}


void DrawForDepth(GLuint depthProg, GLuint VAO, const glm::mat4& model, bool indexed=false, GLuint EBO=0, int count=0)
{
    glUseProgram(depthProg);
    glUniformMatrix4fv(glGetUniformLocation(depthProg, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glBindVertexArray(VAO);
    if (indexed) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, 36); // cube default
    }
    glBindVertexArray(0);
}


GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation error:\n" << infoLog << std::endl;
    }
    return shader;
}

GLuint createShaderProgram()
{
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVS);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFS);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if(!success)
    {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Program linking error:\n" << infoLog << std::endl;
    }


    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}


static glm::vec3 AxisSnapXZ(const glm::vec3& dir)
{
    return glm::vec3(0.f, 0.f, (dir.z >= 0.f) ? 1.f : -1.f);  // Shoot along z axis forward and backward
}




// ============= Main =============
int main() {
    glm::vec3 sunPos = glm::vec3(0.0f, 0.5f, 0.0f);
    float rotationSpeed = 180.0f;
    float angle = 0.0f;
        int lastMouseLeftState = GLFW_RELEASE;
    std::list<Projectile> projectileList;
    float lastFrameTime = glfwGetTime();

    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(1200, 700, "Proj2_v1", nullptr, nullptr);
    if (!win) { std::cerr << "Window creation failed\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    glfwSetCursorPosCallback(win, MouseCB);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::cerr << "GLEW init failed\n"; return -1; }


    GLuint depthCubemap1 = 0, depthCubemap2 = 0;
GLuint depthFBO1 = 0, depthFBO2 = 0;



// create depth cubemap and FBO (do this once at init)
glGenFramebuffers(1, &depthFBO1);
glGenTextures(1, &depthCubemap1);
glBindTexture(GL_TEXTURE_CUBE_MAP, depthCubemap1);
for (unsigned int i = 0; i < 6; ++i) {
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT,
                 SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
}
glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

glBindFramebuffer(GL_FRAMEBUFFER, depthFBO1);
glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthCubemap1, 0);
glDrawBuffer(GL_NONE);
glReadBuffer(GL_NONE);
if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "Depth FBO not complete\n";
glBindFramebuffer(GL_FRAMEBUFFER, 0);


    glEnable(GL_DEPTH_TEST);

    GLuint vs = Compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, kFS);
    GLuint prog = Link(vs, fs);
    //GLuint depthProg = Link(Compile(GL_VERTEX_SHADER, depthVS), Compile(GL_FRAGMENT_SHADER, depthFS));
    glDeleteShader(vs); glDeleteShader(fs);


    GLuint depthVSShader = Compile(GL_VERTEX_SHADER, depthVS);
    GLuint depthGSShader = Compile(GL_GEOMETRY_SHADER, depthGS);
    GLuint depthFSShader = Compile(GL_FRAGMENT_SHADER, depthFS);
    GLuint depthProg = glCreateProgram();
    glAttachShader(depthProg, depthVSShader);
    glAttachShader(depthProg, depthGSShader);
    glAttachShader(depthProg, depthFSShader);
    glLinkProgram(depthProg);
    GLint linkOk = 0;
    glGetProgramiv(depthProg, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        char log[1024];
        glGetProgramInfoLog(depthProg, 1024, nullptr, log);
        std::cerr << "Depth program link error:\n" << log << "\n";
    }
    glDeleteShader(depthVSShader);
    glDeleteShader(depthGSShader);
    glDeleteShader(depthFSShader);


    // Cube
    GLuint cubeVAO=0, cubeVBO=0;
    glGenVertexArrays(1, &cubeVAO);
    glBindVertexArray(cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // Sphere
    std::vector<float> sphereV; std::vector<unsigned int> sphereI;
    GenSphere(0.5f, 32, 32, sphereV, sphereI);
    GLuint sphereVAO=0, sphereVBO=0, sphereEBO=0;
    glGenVertexArrays(1, &sphereVAO);
    glBindVertexArray(sphereVAO);
    glGenBuffers(1, &sphereVBO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, sphereV.size() * sizeof(float), sphereV.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &sphereEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sphereI.size() * sizeof(unsigned int), sphereI.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // Cone
    std::vector<float> coneV; std::vector<unsigned int> coneI;
    GenCone(0.5f, 1.0f, 32, coneV, coneI);
    GLuint coneVAO=0, coneVBO=0, coneEBO=0;
    glGenVertexArrays(1, &coneVAO);
    glBindVertexArray(coneVAO);
    glGenBuffers(1, &coneVBO);
    glBindBuffer(GL_ARRAY_BUFFER, coneVBO);
    glBufferData(GL_ARRAY_BUFFER, coneV.size() * sizeof(float), coneV.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &coneEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, coneEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, coneI.size() * sizeof(unsigned int), coneI.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // Textures
    GLuint texFloor  = LoadTexture("grass.jpg");
    GLuint texSun    = LoadTexture("sun.jpeg");
    GLuint texEarth  = LoadTexture("earth.jpg");
    GLuint texMars   = LoadTexture("mars.jpg");
    GLuint texMoon   = LoadTexture("moon.jpg");
    GLuint texFire   = LoadTexture("fire.jpg");
    GLuint texWater  = LoadTexture("water.jpg");
    GLuint texSoil   = LoadTexture("soil.jpg");
    GLuint texWind   = LoadTexture("wind.jpg");

    // Main loop
    while (!glfwWindowShouldClose(win)) {
        float dt = glfwGetTime() - lastFrameTime;
        lastFrameTime += dt;
        glfwPollEvents();

        

        glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), (float)SHADOW_WIDTH / (float)SHADOW_HEIGHT, SHADOW_NEAR, SHADOW_FAR);
        std::vector<glm::mat4> shadowTransforms1; // For light 1
        shadowTransforms1.push_back(shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)));
        shadowTransforms1.push_back(shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)));
        shadowTransforms1.push_back(shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
        shadowTransforms1.push_back(shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)));
        shadowTransforms1.push_back(shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)));
        shadowTransforms1.push_back(shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f)));



        // glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
        // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float t = float(glfwGetTime());
        lightPos.x  = 10.0f * sinf(t);
        lightPos.z  = 10.0f * cosf(t);
        lightPos2.x = 30.0f * sinf(t);
        lightPos2.z = 30.0f * cosf(t);

        


        float now = float(glfwGetTime());
        deltaTime = now - lastFrame;
        lastFrame = now;

        ProcessWorldInput(win, sunPos);

        // Projection (updates each frame for Z/X zoom)
        glm::mat4 P = glm::perspective(glm::radians(fovDeg), 1200.0f/700.0f, 0.01f, 100.0f);

        // Camera (toggle chase cam with C/V)
        glm::vec3 aimDir;
        glm::mat4 V;
        if (chaseCam) {
            glm::vec3 target = playerPos + glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 back = glm::normalize(glm::vec3(sinf(glm::radians(yawDeg)), 0.0f, cosf(glm::radians(yawDeg))));
            glm::vec3 desired = target + back * 4.0f + glm::vec3(0.0f, 2.0f, 0.0f);
            camPos = glm::mix(camPos, desired, 8.0f * deltaTime);
            camUp = glm::vec3(0.0f, 1.0f, 0.0f);
            V = glm::lookAt(camPos, target, camUp);
            aimDir = glm::normalize(target-camPos);
        } else {
            V = glm::lookAt(camPos, camPos + camFront, camUp);
            aimDir = glm::normalize(camFront);
        }

        // Floor (textured)
        glm::mat4 Mfloor = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.51f, 0.0f)) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(30.0f, 0.02f, 30.0f));

        // Ceiling + walls (flat color)
        glm::vec3 wallColor = glm::vec3(0.529f, 0.808f, 0.922f);
        glm::mat4 Mceiling = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(30.0f, 0.02f, 30.0f));

        glm::mat4 Mw1 = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -15)) * glm::scale(glm::mat4(1.0f), glm::vec3(30, 30, 0.1f));
        glm::mat4 Mw2 = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0,  15)) * glm::scale(glm::mat4(1.0f), glm::vec3(30, 30, 0.1f));
        glm::mat4 Mw3 = glm::translate(glm::mat4(1.0f), glm::vec3(-15, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 30, 30));
        glm::mat4 Mw4 = glm::translate(glm::mat4(1.0f), glm::vec3( 15, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 30, 30));


        // Orrery
        glm::mat4 Mcenter = glm::translate(glm::mat4(1.0f), sunPos) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));

        float r1=5.0f, s1=1.0f;
        glm::vec3 p1 = sunPos + glm::vec3(sinf(t*s1)*r1, 0.0f, cosf(t*s1)*r1);
        glm::mat4 M1 = glm::translate(glm::mat4(1.0f), p1) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));

        float r2=3.0f, s2=0.5f;
        glm::vec3 p2 = sunPos + glm::vec3(sinf(t*s2)*r2, 0.0f, cosf(t*s2)*r2);
        glm::mat4 M2 = glm::translate(glm::mat4(1.0f), p2) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
        

        float r3=0.8f, s3=2.0f;
        glm::vec3 p3 = p1 + glm::vec3(sinf(t*s3)*r3, 0.0f, cosf(t*s3)*r3);
        glm::mat4 M3 = glm::translate(glm::mat4(1.0f), p3) * glm::scale(glm::mat4(1.0f), glm::vec3(0.3f));
        

        // Pillars
        angle += rotationSpeed * deltaTime; if (angle > 360.0f) angle -= 360.0f;
        glm::mat4 Rot = glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(0,1,0));
        float pillarR = 10.0f, pillarSpeed = 0.3f;
        glm::vec3 pillarColor = glm::vec3(0,0,0);
        GLuint pillarTex[4] = { texFire, texWater, texSoil, texWind };

    glm::mat4 Mpillar[4];
glm::vec3 pillarPos[4];

for (int i = 0; i < 4; ++i) {
    float aOff = glm::radians(90.0f * float(i));
    float a = t * pillarSpeed + aOff;
    float x = sinf(a) * pillarR;
    float z = cosf(a) * pillarR;
    pillarPos[i] = glm::vec3(x, 5.0f, z);
    Mpillar[i] = glm::translate(glm::mat4(1.0f), pillarPos[i]) *
                 Rot *
                 glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 10.0f, 1.0f));
}


        

        // Player physics vs pillars
        float groundY = 0.5f;
        float topY = 10.0f;

        ProcessPlayer(win, deltaTime);
        playerVel.y -= gravityC * deltaTime;
        playerPos += playerVel * deltaTime;

        onGround = false; int landed = -1;
        const float pillarRadius = 0.55f;

        for (int i = 0; i < 4; ++i) {
            glm::vec2 pXZ = glm::vec2(playerPos.x, playerPos.z);
            glm::vec2 cXZ = glm::vec2(pillarPos[i].x, pillarPos[i].z);
            float d = glm::length(pXZ - cXZ);
            bool within = (d <= pillarRadius + playerRadiusXZ);
            bool overTop = (playerPos.y <= topY + 0.15f);
            if (within && overTop && playerVel.y <= 0.0f) { landed = i; break; }
        }
        if (landed >= 0) {
            playerPos.y = topY; playerVel.y = 0.0f; onGround = true; onPillar = landed;
        } else if (playerPos.y <= groundY) {
            playerPos.y = groundY; playerVel.y = 0.0f; onGround = true; onPillar = -1;
        }
        if (onPillar >= 0) {
            playerPos.x = pillarPos[onPillar].x;
            playerPos.z = pillarPos[onPillar].z;
        }

        // Character


        





        // 1. render scene to depth cubemap (light 1)
glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
glBindFramebuffer(GL_FRAMEBUFFER, depthFBO1);
glClear(GL_DEPTH_BUFFER_BIT);

glUseProgram(depthProg);

// set shadow matrices
for (unsigned int i = 0; i < 6; ++i)
{
    glUniformMatrix4fv(glGetUniformLocation(depthProg, ("shadowMatrices[" + std::to_string(i) + "]").c_str()),
                       1, GL_FALSE, glm::value_ptr(shadowTransforms1[i]));
}
glUniform3fv(glGetUniformLocation(depthProg, "lightPos"), 1, glm::value_ptr(lightPos));
glUniform1f(glGetUniformLocation(depthProg, "far_plane"), SHADOW_FAR);

DrawForDepth(depthProg, cubeVAO, Mfloor, false, 0, 0);
DrawForDepth(depthProg, cubeVAO, Mceiling, false, 0, 0);

DrawForDepth(depthProg, cubeVAO, Mw1, false, 0, 0);
DrawForDepth(depthProg, cubeVAO, Mw2, false, 0, 0);
DrawForDepth(depthProg, cubeVAO, Mw3, false, 0, 0);
DrawForDepth(depthProg, cubeVAO, Mw4, false, 0, 0);

for (int i = 0; i < 4; ++i) {
    DrawForDepth(depthProg, cubeVAO, Mpillar[i], false, 0, 0);
}

// Остальные объекты
// DrawForDepth(depthProg, cubeVAO, Mbox, false, 0, 0);
// DrawForDepth(depthProg, cubeVAO, Mcube, false, 0, 0);

DrawForDepth(depthProg, sphereVAO, Mcenter, true, sphereEBO, (int)sphereI.size());
DrawForDepth(depthProg, sphereVAO, M1, true, sphereEBO, (int)sphereI.size());
DrawForDepth(depthProg, sphereVAO, M2, true, sphereEBO, (int)sphereI.size());
DrawForDepth(depthProg, sphereVAO, M3, true, sphereEBO, (int)sphereI.size());


// DrawForDepth(depthProg, sphereVAO, Mhead, true, sphereEBO, sphereIndexCount);
// DrawForDepth(depthProg, sphereVAO, MupperArmR, true, sphereEBO, sphereIndexCount);
// DrawForDepth(depthProg, sphereVAO, MupperArmL, true, sphereEBO, sphereIndexCount);
// DrawForDepth(depthProg, sphereVAO, MlowerArmR, true, sphereEBO, sphereIndexCount);
// DrawForDepth(depthProg, sphereVAO, MlowerArmL, true, sphereEBO, sphereIndexCount);
// DrawForDepth(depthProg, sphereVAO, MupperLegR, true, sphereEBO, sphereIndexCount);
// DrawForDepth(depthProg, sphereVAO, MupperLegL, true, sphereEBO, sphereIndexCount);
// DrawForDepth(depthProg, sphereVAO, MlowerLegR, true, sphereEBO, sphereIndexCount);
// DrawForDepth(depthProg, sphereVAO, MlowerLegL, true, sphereEBO, sphereIndexCount);

glBindFramebuffer(GL_FRAMEBUFFER, 0);



            //glViewport(0, 0, windowWidth, windowHeight);
    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(prog);


        glActiveTexture(GL_TEXTURE10);
glBindTexture(GL_TEXTURE_CUBE_MAP, depthCubemap1);
glUniform1i(glGetUniformLocation(prog, "depthMap1"), 10);

glActiveTexture(GL_TEXTURE11);
glBindTexture(GL_TEXTURE_CUBE_MAP, depthCubemap2);
glUniform1i(glGetUniformLocation(prog, "depthMap2"), 11);

glUniform1f(glGetUniformLocation(prog, "far_plane"), SHADOW_FAR);


                glUniform3fv(glGetUniformLocation(prog, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(prog, "viewPos"), 1, glm::value_ptr(camPos));
        glUniform3fv(glGetUniformLocation(prog, "lightPos2"), 1, glm::value_ptr(lightPos2));
        glUniform3fv(glGetUniformLocation(prog, "lightColor2"), 1, glm::value_ptr(lightColor2));
        glm::vec3 lcol = glm::vec3(1.0f);
        glUniform3fv(glGetUniformLocation(prog, "lightColor"), 1, glm::value_ptr(lcol));
        DrawCharacter(prog, sphereVAO, sphereEBO, (int)sphereI.size(),
                      coneVAO, coneEBO, (int)coneI.size(),
                      playerPos, V, P, t);

        DrawObject(prog, sphereVAO, M3, glm::vec3(0,1,0), V, P, texMoon, true, sphereEBO, (int)sphereI.size());
        DrawObject(prog, sphereVAO, M2, glm::vec3(0,0,1), V, P, texMars, true, sphereEBO, (int)sphereI.size());
        DrawObject(prog, sphereVAO, M1, glm::vec3(1,0,0), V, P, texEarth, true, sphereEBO, (int)sphereI.size());
                DrawObject(prog, cubeVAO, Mw1, wallColor, V, P);
        DrawObject(prog, cubeVAO, Mw2, wallColor, V, P);
        DrawObject(prog, cubeVAO, Mw3, wallColor, V, P);
        DrawObject(prog, cubeVAO, Mw4, wallColor, V, P);
        DrawObject(prog, sphereVAO, Mcenter, glm::vec3(1,1,0), V, P, texSun, true, sphereEBO, (int)sphereI.size());
        DrawObject(prog, cubeVAO, Mceiling, wallColor, V, P);
        DrawObject(prog, cubeVAO, Mfloor, glm::vec3(0.0f, 1.0f, 0.0f), V, P, texFloor);
        for (int i = 0; i < 4; ++i) {
    DrawObject(prog, cubeVAO, Mpillar[i], pillarColor, V, P, pillarTex[i]);
}

for (auto& p : projectileList) {
            p.Update(dt);
            p.Draw(prog, cubeVAO, V, P);   
        }

        // Projectile shooting
        if (chaseCam && lastMouseLeftState == GLFW_RELEASE && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {
            const float projectileSpeed = 25.0f;
            glm::vec3 fwd   = AxisSnapXZ(aimDir); 
            glm::vec3 spawn = playerPos + glm::vec3(0.0f, 1.6f, 0.0f) + fwd * 0.7f;

            projectileList.emplace_back(spawn, projectileSpeed * fwd, prog);
        }
        lastMouseLeftState = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);

        glfwSwapBuffers(win);


    }

    // Cleanup
    glDeleteProgram(prog);
    glDeleteVertexArrays(1, &cubeVAO);   glDeleteBuffers(1, &cubeVBO);
    glDeleteVertexArrays(1, &sphereVAO); glDeleteBuffers(1, &sphereVBO); glDeleteBuffers(1, &sphereEBO);
    glDeleteVertexArrays(1, &coneVAO);   glDeleteBuffers(1, &coneVBO);   glDeleteBuffers(1, &coneEBO);

    glDeleteTextures(1, &texFloor);
    glDeleteTextures(1, &texSun);
    glDeleteTextures(1, &texEarth);
    glDeleteTextures(1, &texMars);
    glDeleteTextures(1, &texMoon);
    glDeleteTextures(1, &texFire);
    glDeleteTextures(1, &texWater);
    glDeleteTextures(1, &texSoil);
    glDeleteTextures(1, &texWind);
    glDeleteFramebuffers(1, &depthFBO1);
    glDeleteFramebuffers(1, &depthFBO2);
    glDeleteTextures(1, &depthCubemap1);
    glDeleteTextures(1, &depthCubemap2);

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
