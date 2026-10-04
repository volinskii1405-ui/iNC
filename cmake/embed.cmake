# Превращает файлы шрифтов в массивы C++, чтобы игра была одним бинарником.
# Вызов: cmake -DOUT_DIR=... -DREGULAR=... -DBOLD=... -P embed.cmake
function(embed_file path var out)
    file(READ "${path}" hex HEX)
    file(SIZE "${path}" size)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," arr "${hex}")
    string(APPEND ${out} "extern const unsigned char ${var}[] = {${arr}};\n")
    string(APPEND ${out} "extern const unsigned int ${var}Size = ${size};\n")
    set(${out} "${${out}}" PARENT_SCOPE)
endfunction()

set(src "#include \"EmbeddedFonts.hpp\"\n")
embed_file("${REGULAR}" kFontRegular src)
embed_file("${BOLD}" kFontBold src)
file(WRITE "${OUT_DIR}/EmbeddedFonts.cpp.tmp" "${src}")
file(COPY_FILE "${OUT_DIR}/EmbeddedFonts.cpp.tmp" "${OUT_DIR}/EmbeddedFonts.cpp" ONLY_IF_DIFFERENT)
