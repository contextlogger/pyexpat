
import traceback

try:
    import ElementTree

    doc = u"<doc><hello to=\"world\">foo bar</hello></doc>"
    print doc
    tree = ElementTree.XML(doc)
    print tree
    print ElementTree.tostring(tree)
except:
    traceback.print_exc()
