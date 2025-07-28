#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <list>
#include <vector>

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


//Shaders
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
    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = specularStrength * spec * lightColor;

    vec3 lighting = (ambient + diffuse + specular);

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

glm::vec3 cameraPos   = glm::vec3(0.0f, 0.51f, 1.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);

float deltaTime = 0.0f;
float lastFrame = 0.0f;


// Shaders linking and compilation
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
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

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

    yaw   += xoffset;
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





void processInput(GLFWwindow *window)
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

        //fixing camera at one height to imitate walking
    cameraPos.y = 0.51f;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

}








glm::vec3 lightPos(1.2f, 4.0f, 2.0f);



int main()
{
    float lastFrameTime = glfwGetTime();
    int lastMouseLeftState = GLFW_RELEASE;
    std::list<Projectile> projectileList;
    if (!glfwInit())
    {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }

    // Creating window
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Parallelepiped Scene", nullptr, nullptr);
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
    if(glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to init GLEW\n";
        return -1;
    }

    glEnable(GL_DEPTH_TEST);

    GLuint shaderProgram = createShaderProgram();

    // Creating VAO and VBO
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

// Positions
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);

// Normals
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
glEnableVertexAttribArray(1);

// Texture Coordinates
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
glEnableVertexAttribArray(2);



    glBindVertexArray(0);

    // Camera matrix
    glm::mat4 projectionMatrix = glm::perspective(glm::radians(70.0f), 800.f / 600.f, 0.01f, 100.f);




    ////////////////////////////////////////////////////////////////

            int width, height, nrChannels;
        unsigned char* data = stbi_load("grass.jpg", &width, &height, &nrChannels, 0);
        unsigned int texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        // Wrapping and filtering 
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Loading texture
        if (data) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);
        } else {
            std::cout << "Failed to load texture" << std::endl;
        }
        stbi_image_free(data);


    //////////////////////////////////////////////////////////////









    // Rendering loop
    while (!glfwWindowShouldClose(window))
    {
        float dt = glfwGetTime() - lastFrameTime;
        lastFrameTime += dt;
        glm::vec3 pillarPositions[] = {
        glm::vec3(0.0f, 0.0f, -4.0f),   // center
        glm::vec3(-4.0f, 0.0f, -4.0f), // back-left
        glm::vec3(4.0f, 0.0f, -4.0f),   // back-right
        glm::vec3(-4.0f, 0.0f, 0.0f),
        glm::vec3(4.0f, 0.0f, 0.0f),
        glm::vec3(-4.0f, 0.0f, 4.0f),
        glm::vec3(0.0f, 0.0f, 4.0f),
        glm::vec3(4.0f, 0.0f, 4.0f)   
        };

        glfwPollEvents();

        glClearColor(0.3f, 0.3f, 0.3f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);



        float time = glfwGetTime();
        lightPos.x = 2.0f * sin(time);
        lightPos.z = 2.0f * cos(time);

        glUseProgram(shaderProgram);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1, glm::value_ptr(cameraPos));

        // Drawing objects



        glUseProgram(shaderProgram);

        // Dynamic light
        glm::vec3 lightPos = glm::vec3(sin(glfwGetTime()) * 2.0f, 1.5f, cos(glfwGetTime()) * 2.0f);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightPos"), 1, &lightPos[0]);

        // Setting white color for light
        glm::vec3 lightColor = glm::vec3(1.0f);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightColor"), 1, &lightColor[0]);

        // Camera position
        glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1, &cameraPos[0]);








        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        glm::mat4 viewMatrix = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);


        // Drawing floor
        glm::mat4 floorMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.51f, 0.0f)) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 0.02f, 10.0f));
        glm::vec3 floorColor = glm::vec3(0.0f, 1.0f, 0.0f);

        glm::vec3 wallColor = glm::vec3(0.529f, 0.808f, 0.922f);

        drawColorCube(shaderProgram, VAO, floorMatrix, floorColor, viewMatrix, projectionMatrix, texture);


        glBindTexture(GL_TEXTURE_2D, 0);


        //Drawing ceiling 

        glm::mat4 CeilingMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f)) *
                                  glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 0.02f, 10.0f));



        drawColorCube(shaderProgram, VAO, CeilingMatrix, wallColor, viewMatrix, projectionMatrix );


        // Drawing walls
       

        glm::mat4 wall1 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 10.0f, 0.1f));
        drawColorCube(shaderProgram, VAO, wall1, wallColor, viewMatrix, projectionMatrix);

        glm::mat4 wall2 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 10.0f, 0.1f));
        drawColorCube(shaderProgram, VAO, wall2, wallColor, viewMatrix, projectionMatrix);

        glm::mat4 wall3 = glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 0.0f, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 10.0f, 10.0f));
        drawColorCube(shaderProgram, VAO, wall3, wallColor, viewMatrix, projectionMatrix);

        glm::mat4 wall4 = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 10.0f, 10.0f));
        drawColorCube(shaderProgram, VAO, wall4, wallColor, viewMatrix, projectionMatrix);

        glm::vec3 pillarColor = glm::vec3(0.6f, 0.4f, 0.2f); // brown

        for (int i = 0; i < 9; ++i)
        {
            glm::mat4 pillarMatrix = glm::translate(glm::mat4(1.0f), pillarPositions[i]) *
                                     glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 10.0f, 1.0f));
            drawColorCube(shaderProgram, VAO, pillarMatrix, pillarColor, viewMatrix, projectionMatrix);
        }

        for (std::list<Projectile>::iterator it = projectileList.begin(); it != projectileList.end(); it++) {
            it->Update(dt);
            it->Draw(shaderProgram, VAO, viewMatrix, projectionMatrix);
        }

        if (lastMouseLeftState == GLFW_RELEASE && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            const float projectileSpeed = 25.0f;
            projectileList.push_back(Projectile(cameraPos, projectileSpeed * cameraFront, shaderProgram));
        }
        lastMouseLeftState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);

        

        glfwSwapBuffers(window);



        
    }

    glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &VAO);
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
