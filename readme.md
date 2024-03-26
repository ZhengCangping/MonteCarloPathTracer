![在这里插入图片描述](https://img-blog.csdnimg.cn/direct/468434aa97e1487fa4c9b1673fb6b2a7.bmp)
![在这里插入图片描述](https://img-blog.csdnimg.cn/direct/140a3f973c9c4990a800dfcb5d4cca40.bmp)
![在这里插入图片描述](https://img-blog.csdnimg.cn/direct/cd450ed790854453b206eb6246c2edb9.bmp)
#  0 运行环境
IDE：Visual Studio 2019  
GPU：NVIDIA 2060  
OpenGL+GLFW+glm  
环境配置：  
将项目的include文件夹添加到vs项目属性页的VC++目录-包含目录和C/C++-附加包含目录里。
# 1 预处理
## 1.1 解析场景数据
场景解析通过Model类完成。Model类中的ReadOBJ、ReadMTL、ReadXML方法分别读取.obj（模型）、.mtl（材质）、.xml（摄像机与光源亮度）文件，将获得的场景信息分别赋给Model类中的对应属性。
## 1.2 构建加速结构
程序采用的加速结构是基于SAH的BVH，即在二分构建BVH的基础上引入了SAH优化。  
SAH是一种表面积启发算法，它改变了传统BVH构建时，对面片按照数量进行二分的操作。SAH将包围盒的表面积作为划分的关键，也就是说，对于一组面片，找到一种划分方式使划分后的两组面片的包围盒表面积接近。  
另外，为了便于数据传输至GPU，采用了线性化构建BVH的方法。  

## 1.3 为GPU准备数据
场景数据、BVH、贴图都需要传入GPU。
### 场景数据
场景数据以三角面片为中心重新组织，对应Triangle2Shader结构体。每一个Triangle2Shader的实例对应场景中的一个三角面片，但是它不仅包括obj文件中定义的顶点坐标、顶点法线、纹理坐标信息，还包括了来自mtl的材质信息和来自xml的亮度信息，这通过对1.1中所得场景信息的重组得到。所有的Triangle2Shader实例存储为Model类中的Tris2Shader数组。  
如果某个Triangle2Shader实例的亮度信息大于0，这表明这个三角面片属于光源。将这些实例存储为Model类中的Lights2Shader数组。
### BVH
对BVHNode类中的属性进行部分合并，对应BVHNode2Shader类，存储为Model类中的Nodes2Shader数组。

## 1.4 把数据传入GPU
### 场景数据与BVH
将数组与OpenGL中的Texture Buffer绑定，在shader中使用samplerBuffer访问。Tris2Shader、Lights2Shader和Nodes2Shader都采用了这样的方式。以Tris2Shader为例，具体如下：

 1. 创建一个纹理缓冲对象（Texture Buffer Object，TBO）TBO0，向其中上传Tris2Shader的数据。
 2. 创建一个纹理对象Tris，以TBO0为数据源
 3. 激活一个纹理单元，如GL_TEXTURE1，将其与Tris绑定
 4. 在着色器中，定义一个samplerBuffer，如uniform samplerBuffer Tris
 5. 在渲染循环中设置上述samplerBuffer的值为1（纹理单元的编号），这意味着告诉着色器：“在着色器中，当你看到名为'Tris'的samplerBuffer变量时，请使用纹理单元1中的数据。”即实现了数据的绑定
 6. 在着色器中，使用texelFetch从samplerBuffer中获取数据
### 纹理
通过stb_image读取图片。将每张图片绑定为一个OpenGL纹理目标（GL_TEXTURE_2D），在着色器中通过uniform sampler2D访问。

## 1.5 布置画布与Draw Call
在OpenGL中，当glDrawArrays()和glDrawElements()这样的绘制函数被调用时，就会触发Draw Call。由于这些绘制函数的对象是顶点数组，而路径追踪的绘制对象是像素——这一过程应该发生在片元着色器上。因此我们需要通过布置一个画布来实现二者的沟通。设置一个长度为6的顶点数组，它对应了2个三角形，这就是画布：  
    float vertices[] = {  
        -1.0f, -1.0f, 0.0f,  
         1.0f, -1.0f, 0.0f,  
        -1.0f,  1.0f, 0.0f,  
         1.0f,  1.0f, 0.0f,  
        -1.0f,  1.0f, 0.0f,  
         1.0f, -1.0f, 0.0f  
    };  
你可以理解为：OpenGL要求绘制2个三角形，而通过片元着色器上的定义，我们让三角形所覆盖的每个像素的颜色由路径追踪所获得。  
另外，顶点着色器中的out变量将在插值后传给片元着色器中对应的in变量。这一插值的特性可以帮助我们轻松地获取每个像素在画布上的坐标。  

# 2 shader 路径追踪
路径追踪的过程发生在片元着色器（fragment shader）上。  
经过CPU与GPU的通信，shader中已经拥有了如下数据：  

 - 场景：uniform samplerBuffer Tris
 - BVH：uniform samplerBuffer Tree
 - 灯光：uniform samplerBuffer Lights
 - 纹理：uniform sampler2D texture0等
 - 相机：uniform vec3 eye、uniform mat4 view等
## 2.1 生成光线
对屏幕中的每个像素生成**一条**从相机出发、穿过像素的光线，之后对该光线做路径追踪。  
注：在一般的路径追踪里，会对一个像素生成多条光线（光线数称为采样数，SPP），最终将所有光线的radiance取平均作为像素的radiance。本项目之所以只生成一条光线，是因为采用了多帧混合的做法，即每帧的结果都由之前的绘制结果与当前帧的绘制结果混合得到。也就是说，虽然每帧只进行了1SPP的路径追踪，但总的采样数却与帧数一起增加，在运行时可以看到明显的收敛过程。  
光线从相机的视点出发，在像素内的落点通过随机数得到。为了避免伪随机，使用sobol序列代替伪随机序列。  
## 2.2 模拟递归
由于着色器不支持函数的递归调用，我们不得不对路径追踪的结构进行更改。可以将递归更改为从0~maxBounce（自己设置的一根光线的最大弹射次数）的循环，并在循环外用一个history变量记录之前着色的结果。  

## 2.3 直接光照
直接光照是来自光源的radiance，将其转换为对光源采样来进行计算。  
在光源的每个三角面上采样一个点，PDF=1/A，A为三角面的面积。计算该点对着色点的radiance，将所有三角面产生的radiance累加，即求得直接光照。  
在光源的三角面过多时，这样计算显然开销太大。因此可以随机选择n个三角面，用如下方式近似：  
（这n个三角面产生的radiance的平均值）x 三角面数量 ≈ 总radiance  
## 2.4 间接光照
间接光照是来自于其他表面折射、反射的radiance。  
首先，根据着色点的材质，采样得到从着色点弹出的光线、对该光线继续进行路径追踪，并采用蒙特卡洛方法估计间接光照。  
## 2.5 计算f_r与确定next ray
漫反射：f_r = kd/pi  
镜面反射：f_r = ks  

确定next ray：  
当以漫反射概率采样时，使用的采样策略是余弦加权的均匀半球采样。  
当以镜面反射概率采样时，使用的是Disney的重要性采样。  
## 2.6 终止条件
 1. russian roulette：以p的概率终止路径追踪  
 2. 光线击中光源  
 3. 光线没有击中任何物体  
 4. 光线弹射次数超过设置的值  
#  3 多帧混合渲染
在简单的OpenGL程序里，我们用一个fragment shader实现所有的片元着色流程，直接将计算得到的结果赋给fragcolor。此时，每帧绘制的结果是独立的。而我们想要让当前帧的结果是过去所有路径追踪结果的平均，也就是说，我们应该保存之前的绘制结果，并在渲染每一帧时与当前帧进行混合。这就是多帧混合渲染，它通过FBO的渲染到纹理实现。
##  3.1 多帧混合管线
![在这里插入图片描述](https://img-blog.csdnimg.cn/direct/1107ddde7c734fd98b21da04ac9900db.png)
具体来说，lastframe和update是2D纹理对象，将其与FBO绑定，可以使OpenGL将渲染结果输出到纹理对象，而非直接输出到屏幕。在每一次render中，都要经历从shader1到shader3的渲染管线。首先，shader1将本次路径追踪的结果与lastframe进行混合，输出到FBO1所绑定的update纹理中；其次，shader2将update中的结果输出到FBO2所绑定的lastframe中，实现lastframe的更新；同时，shader3将shader2所输出的lastframe进行后处理（如伽马矫正），并绘制到屏幕。
##  3.2 与上一帧混合
shader1将本次路径追踪的结果与lastframe进行混合，这一混合的方式为 mix(last, color, 1.0f / float(frameCounter + 1));   
按照 1/总帧数 的权重将本次路径追踪结果与上一帧混合，这保证了之前每次路径追踪结果在本帧中占比相同，也即实现了对各次路径追踪结果的平均。  
#  4 实验结果
##  4.1 漫反射场景cornell-box
顶点数：245974  
面数：102988  
读取场景信息耗时：10.67s  
构建BVH耗时：14.89s  
render loop前总耗时：25.88s  
图像分辨率：1024x1024  
平均绘制一帧耗时：0.4s  
![在这里插入图片描述](https://img-blog.csdnimg.cn/direct/468434aa97e1487fa4c9b1673fb6b2a7.bmp)
##  4.2 镜面反射场景veach-mis
顶点数：1860  
面数：3092  
读取场景信息耗时：0.14s  
构建BVH耗时：0.24s  
render loop前总耗时：0.63s  
图像分辨率：1280x720  
平均绘制一帧耗时：0.02s  
![在这里插入图片描述](https://img-blog.csdnimg.cn/direct/140a3f973c9c4990a800dfcb5d4cca40.bmp)
##  4.3 带贴图的复杂场景bathroom
顶点数：361435  
面数：592188  
读取场景信息耗时：28.22s  
构建BVH耗时：121.78s  
render loop前总耗时：151.89s 
图像分辨率：512x512
平均绘制一帧耗时：1.9s  
![在这里插入图片描述](https://img-blog.csdnimg.cn/direct/cd450ed790854453b206eb6246c2edb9.bmp)
