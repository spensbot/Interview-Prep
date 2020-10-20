#include "rtweekend.h"
#include "color.h"
#include "hittable_list.h"
#include "sphere.h"
#include "camera.h"
#include "material.h"
#include "image.h"
#include "timer.h"

#include <mutex>
#include <thread>
#include <iostream>
#include <atomic>
#include <functional>

//Image Quality
const int image_width = 600;
const int samples_per_pixel_total = 16;

const int threadCount = 16;
const int samples_per_pixel = samples_per_pixel_total / threadCount;

const auto aspect_ratio = 16.0 / 9.0;
const int image_height = static_cast<int>(image_width / aspect_ratio);
const int max_depth = 10;

// Camera
point3 lookfrom(13, 2, 3);
point3 lookat(0, 0, 0);
vec3 vup(0, 1, 0);
auto dist_to_focus = 10.0;
auto aperture = 0.1;

camera cam(lookfrom, lookat, vup, 20, aspect_ratio, aperture, dist_to_focus);

color ray_color(const ray &r, const hittable &world, int depth)
{
  hit_record rec;

  if (depth <= 0)
  {
    return color(0.0, 0.0, 0.0);
  }

  if (world.hit(r, 0.001, infinity, rec))
  {
    ray scattered;
    color attenuation;
    if (rec.mat_ptr->scatter(r, rec, attenuation, scattered))
      return attenuation * ray_color(scattered, world, depth - 1);
    return color(0, 0, 0);
  }

  vec3 unit_direction = unit_vector(r.direction());
  auto t = 0.5 * (unit_direction.y() + 1.0);
  return (1.0 - t) * color(1.0, 1.0, 1.0) + t * color(0.5, 0.7, 1.0);
}

hittable_list random_scene()
{
  hittable_list world;

  auto ground_material = make_shared<lambertian>(color(0.5, 0.5, 0.5));
  world.add(make_shared<sphere>(point3(0, -1000, 0), 1000, ground_material));

  for (int a = -11; a < 11; a++)
  {
    for (int b = -11; b < 11; b++)
    {
      auto choose_mat = random_double();
      point3 center(a + 0.9 * random_double(), 0.2, b + 0.9 * random_double());

      if ((center - point3(4, 0.2, 0)).length() > 0.9)
      {
        shared_ptr<material> sphere_material;

        if (choose_mat < 0.8)
        {
          // diffuse
          auto albedo = color::random() * color::random();
          sphere_material = make_shared<lambertian>(albedo);
          world.add(make_shared<sphere>(center, 0.2, sphere_material));
        }
        else if (choose_mat < 0.95)
        {
          // metal
          auto albedo = color::random(0.5, 1);
          auto fuzz = random_double(0, 0.5);
          sphere_material = make_shared<metal>(albedo, fuzz);
          world.add(make_shared<sphere>(center, 0.2, sphere_material));
        }
        else
        {
          // glass
          sphere_material = make_shared<dielectric>(1.5);
          world.add(make_shared<sphere>(center, 0.2, sphere_material));
        }
      }
    }
  }

  auto material1 = make_shared<dielectric>(1.5);
  world.add(make_shared<sphere>(point3(0, 1, 0), 1.0, material1));

  auto material2 = make_shared<lambertian>(color(0.4, 0.2, 0.1));
  world.add(make_shared<sphere>(point3(-4, 1, 0), 1.0, material2));

  auto material3 = make_shared<metal>(color(0.7, 0.6, 0.5), 0.0);
  world.add(make_shared<sphere>(point3(4, 1, 0), 1.0, material3));

  return world;
}

class ThreadSafeProgress {
public:
  static ThreadSafeProgress& singleton(){
    static ThreadSafeProgress instance;
    return instance;
  }

  static void init(int threadCount) {
    for (int i=0 ; i<threadCount ; i++) {
      singleton().linesLeft.push_back(0);
    }
    singleton().timer.reset();
  }

  void update(int thread, int lineCount) {
    std::lock_guard<std::mutex> guard(threadProgressMutex);

    if(thread == 0) {
      auto elapsed = singleton().timer.getElapsedS();
      estimatedTime = elapsed * lineCount;
      singleton().timer.reset();
    }

    linesLeft[thread] = lineCount;
    std::cerr << "\rScanlines remaining: ";
    
    for (auto line : linesLeft) {
      std::cerr << line << ' ';
    }

    std::cerr << "Estimated: " << estimatedTime / 60.0 << " minutes";

    std::cerr << std::flush;
  }

private:
  std::mutex threadProgressMutex;
  std::vector<int> linesLeft;
  Timer timer;
  double estimatedTime = 0.0; // seconds
};

void renderImage(const hittable_list& world, const unsigned int threadIndex, Image* image){
  for (int j = image_height - 1; j >= 0; --j)
  {
    ThreadSafeProgress::singleton().update(threadIndex, j);
    for (int i = 0; i < image_width; ++i)
    {
      color pixel_color(0, 0, 0);
      for (int s = 0; s < samples_per_pixel; ++s)
      {
        auto u = (i + random_double()) / (image_width - 1);
        auto v = (j + random_double()) / (image_height - 1);
        ray r = cam.get_ray(u, v);
        pixel_color += ray_color(r, world, max_depth);
      }
      Pixel pixel = get_color(pixel_color, samples_per_pixel);
      
      image->pushPixel(pixel);
    }
  }
}

int main()
{
  std::vector<std::thread> threads;
  ThreadSafeProgress::init(threadCount);
  std::vector<Image> images;

  // World
  auto world = random_scene();

  for (int i=0 ; i<threadCount ; i++) {
    images.push_back(Image(image_width, image_height));
  }

  for (int i=0 ; i<threadCount ; i++) {
    threads.push_back(std::thread(renderImage, world, i, &images[i]));
  }

  for (auto& thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  Image::average(images).output();
  // images[0].output();
  // Image::average(images[0], images[1]).output();

  std::cerr << "\nDone.\n";
}