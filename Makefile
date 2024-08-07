OUT := vl

CC := cc
LIBS := -lglfw -lvulkan
FLAGS := -Wall -Wextra -std=c99 -O2 -g

SHADER := shaders

.PHONY: clean shader mk_shader

$(OUT): main.c
	$(CC) $(FLAGS) $(LIBS) -o $@ $^

shader: mk_shader shaders/vert.spv shaders/frag.spv

mk_shader:
	mkdir -p $(SHADER)

$(SHADER)/vert.spv: shader.vert
	glslc $< -o $@

$(SHADER)/frag.spv: shader.frag
	glslc $< -o $@

clean:
	rm -rf $(OUT)
	rm -rf $(SHADER)
