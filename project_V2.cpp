#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <list>
#include <vector>

/*class Projectile
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
        // this is a bit of a shortcut, since we have a single vbo, it is already bound
        // let's just set the world matrix in the vertex shader
        
        glm::mat4 worldMatrix = glm::translate(glm::mat4(1.0f), mPosition) * glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.2f, 0.2f, 0.2f));
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "worldMatrix"), 1, GL_FALSE, &worldMatrix[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "viewMatrix"), 1, GL_FALSE, &viewMatrix[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projectionMatrix"), 1, GL_FALSE, &projectionMatrix[0][0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "objectColor"), 1, glm::value_ptr(glm::vec3(1.0f, 0.0f, 0.0f))); // Yellow projectile
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
*/

// Shaders
const char* vertexShaderSource = R"(
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
    FragPos = vec3(worldMatrix * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(worldMatrix))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = projectionMatrix * viewMatrix * vec4(FragPos, 1.0);
})";

const char* fragmentShaderSource = R"(
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

void main() {
    // Ambient
    float ambientStrength = 0.2;
    vec3 ambient = ambientStrength * lightColor;

    // Diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // Specular
    float specularStrength = 0.2;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = specularStrength * spec * lightColor;

    vec3 lightDir2 = normalize(lightPos2 - FragPos);
    float diff2 = max(dot(norm, lightDir2), 0.0);
    vec3 diffuse2 = diff2 * lightColor2;

    vec3 reflectDir2 = reflect(-lightDir2, norm);
    float spec2 = pow(max(dot(viewDir, reflectDir2), 0.0), 32.0);
    vec3 specular2 = specularStrength * spec2 * lightColor2;

    vec3 lighting = (ambient + diffuse + specular + diffuse2 + specular2);

    vec3 baseColor = useTexture ? texture(texture1, TexCoord).rgb : objectColor;

    vec3 result = lighting * baseColor;
    FragColor = vec4(result, 1.0);
}
)";

float lastX = 800.0f / 2.0f;
float lastY = 600.0f / 2.0f;
bool firstMouse = true;

float yaw = -90.0f;
float pitch = 0.0f;

glm::vec3 cameraPos = glm::vec3(0.0f, 0.51f, 1.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Shader compilation
GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation error:\n" << infoLog << std::endl;
    }
    return shader;
}

GLuint createShaderProgram()
{
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Program linking error:\n" << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

// Cube vertices
float cubeVertices[] = {
    // Position          // Normals           // Texture coordinates
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,

    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,

    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,

    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f
};

//Draw function
void drawColorCube(GLuint shaderProgram, GLuint VAO,
                   const glm::mat4& worldMatrix,
                   const glm::vec3& color,
                   const glm::mat4& viewMatrix,
                   const glm::mat4& projectionMatrix,
                    GLuint texture =0)
{
    glUseProgram(shaderProgram);

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "worldMatrix"), 1, GL_FALSE, &worldMatrix[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "viewMatrix"), 1, GL_FALSE, &viewMatrix[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projectionMatrix"), 1, GL_FALSE, &projectionMatrix[0][0]);
    glUniform3fv(glGetUniformLocation(shaderProgram, "objectColor"), 1, &color[0]);


    if(texture !=0)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0);
    }
    else{
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 0);
    }

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);


    if(texture != 0){
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}


// Sphere generation function
void generateSphere(float radius, int sectors, int stacks, std::vector<float>& vertices, std::vector<unsigned int>& indices)
{
    const float PI = 3.14159265359f;
    vertices.clear();
    indices.clear();

    for (int i = 0; i <= stacks; ++i)
    {
        float v = i / (float)stacks;
        float phi = v * PI;

        for (int j = 0; j <= sectors; ++j)
        {
            float u = j / (float)sectors;
            float theta = u * 2.0f * PI;

            float x = cos(theta) * sin(phi);
            float y = cos(phi);
            float z = sin(theta) * sin(phi);

            // Position
            vertices.push_back(x * radius);
            vertices.push_back(y * radius);
            vertices.push_back(z * radius);

            // Normal (same as position for a unit sphere)
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);

            // Texture coordinates
            vertices.push_back(u);
            vertices.push_back(v);
        }
    }

    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < sectors; ++j)
        {
            int k1 = i * (sectors + 1) + j;
            int k2 = k1 + sectors + 1;

            indices.push_back(k1);
            indices.push_back(k2);
            indices.push_back(k1 + 1);

            indices.push_back(k1 + 1);
            indices.push_back(k2);
            indices.push_back(k2 + 1);
        }
    }
}

// Function to load a texture
GLuint loadTexture(const char* filename)
{
    int width, height, nrChannels;
    unsigned char* data = stbi_load(filename, &width, &height, &nrChannels, 0);
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (data)
    {
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        std::cerr << "Failed to load texture: " << filename << std::endl;
    }
    stbi_image_free(data);
    return texture;
}

// Draw function for cubes and spheres
void drawObject(GLuint shaderProgram, GLuint VAO, const glm::mat4& worldMatrix, const glm::vec3& color, 
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, GLuint texture = 0, bool isSphere = false, GLuint EBO = 0, int indexCount = 0)
{
    glUseProgram(shaderProgram);

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "worldMatrix"), 1, GL_FALSE, &worldMatrix[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "viewMatrix"), 1, GL_FALSE, &viewMatrix[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projectionMatrix"), 1, GL_FALSE, &projectionMatrix[0][0]);
    glUniform3fv(glGetUniformLocation(shaderProgram, "objectColor"), 1, &color[0]);

    if (texture != 0)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0);
    }
    else
    {
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 0);
    }

    glBindVertexArray(VAO);
    if (isSphere)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    }
    else
    {
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glBindVertexArray(0);

    if (texture != 0)
    {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.5f; //increasing speed
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void processInput(GLFWwindow* window)
{
    float cameraSpeed = 3.0f * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;

    cameraPos.y = 0.51f;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
}

glm::vec3 lightPos(1.2f, 2.5f, 1.5f);
glm::vec3 lightPos2 = glm::vec3(25.2f, 2.5f, 25.5f);
glm::vec3 lightColor2 = glm::vec3(1.0f, 0.0f, 0.0f);

int main()
{
    float rotationSpeed = 180.0f;  // 180 degrees per second
    float angle = 0;
    float lastFrameTime = glfwGetTime();
    float dt = glfwGetTime() - lastFrameTime;
    int lastMouseLeftState = GLFW_RELEASE;
    //std::list<Projectile> projectileList;
    if (!glfwInit())
    {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1200, 700, "Assignment 1", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    glfwSetCursorPosCallback(window, mouse_callback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to init GLEW\n";
        return -1;
    }

    glEnable(GL_DEPTH_TEST);

    GLuint shaderProgram = createShaderProgram();

    // Cube VAO and VBO
    GLuint cubeVAO, cubeVBO;
    glGenVertexArrays(1, &cubeVAO);
    glBindVertexArray(cubeVAO);

    glGenBuffers(1, &cubeVBO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Sphere VAO, VBO, and EBO
    std::vector<float> sphereVertices;
    std::vector<unsigned int> sphereIndices;
    generateSphere(0.5f, 32, 32, sphereVertices, sphereIndices);

    GLuint sphereVAO, sphereVBO, sphereEBO;
    glGenVertexArrays(1, &sphereVAO);
    glBindVertexArray(sphereVAO);

    glGenBuffers(1, &sphereVBO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, sphereVertices.size() * sizeof(float), sphereVertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &sphereEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sphereIndices.size() * sizeof(unsigned int), sphereIndices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    //rotating pillars
    angle = (angle + rotationSpeed * dt);
    glm::mat4 pillarsRotation = glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(0.0f, 1.0f, 0.0f));
    

    // Load textures
    GLuint floorTexture = loadTexture("grass.jpg");
    GLuint sunTexture = loadTexture("sun.jpeg");
    GLuint planet1Texture = loadTexture("earth.jpg");
    GLuint planet2Texture = loadTexture("mars.jpg");
    GLuint moonTexture = loadTexture("moon.jpg");
    GLuint fireTexture = loadTexture("fire.jpg");
    GLuint waterTexture = loadTexture("water.jpg");
    GLuint soilTexture = loadTexture("soil.jpg");
    GLuint windTexture = loadTexture("wind.jpg");

    glm::mat4 projectionMatrix = glm::perspective(glm::radians(70.0f), 1200.0f / 700.0f, 0.01f, 100.0f);

    // Rendering loop
    while (!glfwWindowShouldClose(window))
    {
        
        lastFrameTime += dt;
        glm::vec3 pillarPositions[] = { //positions for pillars
        //glm::vec3(0.0f, 0.0f, -4.0f),   
        glm::vec3(-8.0f, 6.0f, -8.0f), 
        glm::vec3(8.0f, 6.0f, -8.0f),   
        //glm::vec3(-4.0f, 0.0f, 0.0f),
        //glm::vec3(4.0f, 0.0f, 0.0f),
        glm::vec3(-8.0f, 6.0f, 8.0f),
        //glm::vec3(0.0f, 0.0f, 4.0f),
        glm::vec3(8.0f, 6.0f, 8.0f)   
        };

        glfwPollEvents();

        glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float time = glfwGetTime();
        lightPos.x = 10.0f * sin(time);
        lightPos.z = 10.0f * cos(time);

        lightPos2.x = 30.0f * sin(time);
        lightPos2.z = 30.0f * cos(time);

        glUseProgram(shaderProgram);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1, glm::value_ptr(cameraPos));
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightPos2"), 1, glm::value_ptr(lightPos2));
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightColor2"), 1, glm::value_ptr(lightColor2));

        glm::vec3 lightColor = glm::vec3(1.0f);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightColor"), 1, &lightColor[0]);

        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        glm::mat4 viewMatrix = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

        // Draw floor
        glm::mat4 floorMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.51f, 0.0f)) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(30.0f, 0.02f, 30.0f));
        glm::vec3 floorColor = glm::vec3(0.0f, 1.0f, 0.0f);
        drawObject(shaderProgram, cubeVAO, floorMatrix, floorColor, viewMatrix, projectionMatrix, floorTexture);

        // Draw ceiling
        glm::mat4 ceilingMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)) *
                                  glm::scale(glm::mat4(1.0f), glm::vec3(30.0f, 0.02f, 30.0f));
        glm::vec3 wallColor = glm::vec3(0.529f, 0.808f, 0.922f);
        drawObject(shaderProgram, cubeVAO, ceilingMatrix, wallColor, viewMatrix, projectionMatrix);

        // Draw walls
        glm::mat4 wall1 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -15.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(30.0f, 30.0f, 0.1f));
        drawObject(shaderProgram, cubeVAO, wall1, wallColor, viewMatrix, projectionMatrix);

        glm::mat4 wall2 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 15.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(30.0f, 30.0f, 0.1f));
        drawObject(shaderProgram, cubeVAO, wall2, wallColor, viewMatrix, projectionMatrix);

        glm::mat4 wall3 = glm::translate(glm::mat4(1.0f), glm::vec3(-15.0f, 0.0f, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 30.0f, 30.0f));
        drawObject(shaderProgram, cubeVAO, wall3, wallColor, viewMatrix, projectionMatrix);

        glm::mat4 wall4 = glm::translate(glm::mat4(1.0f), glm::vec3(15.0f, 0.0f, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 30.0f, 30.0f));
        drawObject(shaderProgram, cubeVAO, wall4, wallColor, viewMatrix, projectionMatrix);

        // Draw spheres
        // Central sphere

        glm::vec3 sunPosition = glm::vec3(0.0f, 0.5f, 0.0f);

glm::mat4 centerSphereMatrix = glm::translate(glm::mat4(1.0f), sunPosition) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
    glm::vec3 centerSphereColor = glm::vec3(1.0f, 1.0f, 0.0f); // Yellow (fallback)
    drawObject(shaderProgram, sphereVAO, centerSphereMatrix, centerSphereColor, viewMatrix, projectionMatrix, sunTexture, true, sphereEBO, sphereIndices.size());

    // Orbiting sphere 1 (Planet 1)
    float orbitRadius1 = 5.0f;
    float orbitSpeed1 = 1.0f;
    glm::vec3 orbitSphere1RelativePos = glm::vec3(sin(time * orbitSpeed1) * orbitRadius1, 0.0f, cos(time * orbitSpeed1) * orbitRadius1);
    glm::vec3 orbitSphere1Pos = sunPosition + orbitSphere1RelativePos;
    glm::mat4 orbitSphere1Matrix = glm::translate(glm::mat4(1.0f), orbitSphere1Pos) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    glm::vec3 orbitSphere1Color = glm::vec3(1.0f, 0.0f, 0.0f); // Red (fallback)
    drawObject(shaderProgram, sphereVAO, orbitSphere1Matrix, orbitSphere1Color, viewMatrix, projectionMatrix, planet1Texture, true, sphereEBO, sphereIndices.size());

    // Orbiting sphere 2 (Planet 2)
    float orbitRadius2 = 3.0f;
    float orbitSpeed2 = 0.5f;
    glm::vec3 orbitSphere2RelativePos = glm::vec3(sin(time * orbitSpeed2) * orbitRadius2, 0.0f, cos(time * orbitSpeed2) * orbitRadius2);
    glm::vec3 orbitSphere2Pos = sunPosition + orbitSphere2RelativePos;
    glm::mat4 orbitSphere2Matrix = glm::translate(glm::mat4(1.0f), orbitSphere2Pos) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    glm::vec3 orbitSphere2Color = glm::vec3(0.0f, 0.0f, 1.0f); // Blue (fallback)
    drawObject(shaderProgram, sphereVAO, orbitSphere2Matrix, orbitSphere2Color, viewMatrix, projectionMatrix, planet2Texture, true, sphereEBO, sphereIndices.size());

    // Moon orbiting Planet 1
    float orbitRadius3 = 0.8f;
    float orbitSpeed3 = 2.0f;
    glm::vec3 orbitSphere3RelativePos = glm::vec3(sin(time * orbitSpeed3) * orbitRadius3, 0.0f, cos(time * orbitSpeed3) * orbitRadius3);
    glm::vec3 orbitSphere3Pos = orbitSphere1Pos + orbitSphere3RelativePos;
    glm::mat4 orbitSphere3Matrix = glm::translate(glm::mat4(1.0f), orbitSphere3Pos) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(0.3f));
    glm::vec3 orbitSphere3Color = glm::vec3(0.0f, 1.0f, 0.0f); // Green (fallback)
    drawObject(shaderProgram, sphereVAO, orbitSphere3Matrix, orbitSphere3Color, viewMatrix, projectionMatrix, moonTexture, true, sphereEBO, sphereIndices.size());
        //Draw pillars
        glm::vec3 pillarColor = glm::vec3(0.0f, 0.0f, 0.0f); // black
        GLuint textures[] = {fireTexture, waterTexture, soilTexture, windTexture};


        angle += rotationSpeed * deltaTime;
        if (angle > 360.0f) {
            angle -= 360.0f;
        }

        glm::mat4 pillarsRotation = glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(0.0f, 1.0f, 0.0f));
        float pillarOrbitRadii[] = { 10.0f, 10.0f, 10.0f, 10.0f };
        float pillarOrbitSpeeds[] = { 0.3f, 0.3f, 0.3f, 0.3f };

        // Draw pillars with rotation applied
        for (int i = 0; i < 4; ++i)
        {
            float angleOffset = glm::radians(90.0f * i); 
            float anglePillar = time * pillarOrbitSpeeds[i] + angleOffset;
            float x = sin(anglePillar) * pillarOrbitRadii[i];
            float z = cos(anglePillar) * pillarOrbitRadii[i];
            glm::vec3 position = glm::vec3(x, 5.0f, z);
            
            glm::mat4 pillarMatrix =
            glm::translate(glm::mat4(1.0f), position) * pillarsRotation * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 10.0f, 1.0f));

            drawColorCube(shaderProgram, cubeVAO, pillarMatrix, pillarColor, viewMatrix, projectionMatrix, textures[i]);
            
        }

        /*for (std::list<Projectile>::iterator it = projectileList.begin(); it != projectileList.end(); it++) {
            it->Update(dt);
            it->Draw(shaderProgram, cubeVAO, viewMatrix, projectionMatrix);
        }

        if (lastMouseLeftState == GLFW_RELEASE && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            const float projectileSpeed = 25.0f;
            projectileList.push_back(Projectile(cameraPos, projectileSpeed * cameraFront, shaderProgram));
        }*/
        lastMouseLeftState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);


        glfwSwapBuffers(window);
    }

    glDeleteBuffers(1, &cubeVBO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteProgram(shaderProgram);
    glDeleteTextures(1, &floorTexture);
    glDeleteTextures(1, &sunTexture);
    glDeleteTextures(1, &planet1Texture);
    glDeleteTextures(1, &planet2Texture);
    glDeleteTextures(1, &moonTexture);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}