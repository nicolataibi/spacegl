local image_map = {}

local file = io.open("pdf-assets-map.tsv", "r")
if not file then
    error("Manca pdf-assets-map.tsv: eseguire prepare-pdf-assets.py")
end

for line in file:lines() do
    local original, alias = line:match("^(.-)\t(.-)$")
    if original and alias then
        image_map[original] = alias
        image_map["./" .. original] = alias
    end
end
file:close()

local function decode_url(s)
    return (s:gsub("%%(%x%x)", function(hex)
        return string.char(tonumber(hex, 16))
    end))
end

local function resolve_image(src)
    local decoded = decode_url(src):gsub("^%./", "")
    return image_map[decoded] or image_map[src] or src
end

local function rewrite_image(img)
    img.src = resolve_image(img.src)
    return img
end

local function rewrite_html_images(html)
    -- Riscrive i percorsi delle immagini prima della conversione HTML.
    html = html:gsub("<%s*[Ii][Mm][Gg][^>]*>", function(tag)
        tag = tag:gsub('(src%s*=%s*")([^"]*)(")', function(prefix, src, suffix)
            return prefix .. resolve_image(src) .. suffix
        end)

        tag = tag:gsub("(src%s*=%s*')([^']*)(')", function(prefix, src, suffix)
            return prefix .. resolve_image(src) .. suffix
        end)

        -- Elimina sempre le altezze fisse: preserviamo le proporzioni.
        tag = tag:gsub("%s+[Hh][Ee][Ii][Gg][Hh][Tt]%s*=%s*\"[^\"]*\"", "")
        tag = tag:gsub("%s+[Hh][Ee][Ii][Gg][Hh][Tt]%s*=%s*'[^']*'", "")
        tag = tag:gsub("%s+[Hh][Ee][Ii][Gg][Hh][Tt]%s*=%s*[^%s>]+", "")

        -- La larghezza dipende dal numero di celle della riga HTML.
        local _, cells = html:gsub("<%s*[Tt][Dd][%s>]", "")
        local _, headers = html:gsub("<%s*[Tt][Hh][%s>]", "")
        cells = cells + headers

        local target_width = nil
        if cells >= 3 then
            target_width = "28%"
        elseif cells == 2 then
            target_width = "45%"
        end

        -- Rimuove la larghezza HTML precedente, spesso troppo grande.
        tag = tag:gsub("%s+[Ww][Ii][Dd][Tt][Hh]%s*=%s*\"[^\"]*\"", "")
        tag = tag:gsub("%s+[Ww][Ii][Dd][Tt][Hh]%s*=%s*'[^']*'", "")
        tag = tag:gsub("%s+[Ww][Ii][Dd][Tt][Hh]%s*=%s*[^%s>]+", "")

        -- Nelle righe a colonne assegna una larghezza proporzionata.
        if target_width then
            tag = tag:gsub("%s*/?>$", function(ending)
                if ending:match("/>") then
                    return ' width="' .. target_width .. '" />'
                end
                return ' width="' .. target_width .. '">'
            end)
        end

        return tag
    end)

    return html
end

function Image(img)
    return rewrite_image(img)
end

function RawBlock(el)
    if el.format ~= "html" then
        return nil
    end

    if not (
        el.text:match("<%s*[Tt][Aa][Bb][Ll][Ee]")
        or el.text:match("<%s*[Tt][Rr][%s>]")
        or el.text:match("<%s*[Ii][Mm][Gg][%s/>]")
        or el.text:match("<%s*[Ff][Ii][Gg][Uu][Rr][Ee]")
    ) then
        return nil
    end

    local html = rewrite_html_images(el.text)
    local ok, doc = pcall(pandoc.read, html, "html")

    if not ok or not doc then
        io.stderr:write("Avviso: blocco HTML non convertito.\n")
        return nil
    end

    doc = doc:walk({ Image = rewrite_image })
    return doc.blocks
end
