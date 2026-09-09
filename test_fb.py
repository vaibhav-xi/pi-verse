import os
import pygame

WIDTH = 480
HEIGHT = 320
FB = "/dev/fb1"

os.environ["SDL_VIDEODRIVER"] = "dummy"

pygame.init()
pygame.display.set_mode((1, 1))

screen = pygame.Surface(
    (WIDTH, HEIGHT),
    depth=16,
    masks=(0xF800, 0x07E0, 0x001F, 0),
)

screen.fill((255, 0, 0))

raw = screen.get_buffer().raw

print("Surface size:", screen.get_size())
print("Surface bytes:", len(raw))
print("Expected bytes:", WIDTH * HEIGHT * 2)
print("Bytesize:", screen.get_bytesize())
print("Pitch:", screen.get_pitch())

with open(FB, "wb") as fb:
    written = fb.write(raw)
    fb.flush()

print("Bytes written:", written)
