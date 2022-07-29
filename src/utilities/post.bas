   10 bload "b65support.bin":sys $1600
   20 f$="hello.el"
   30 l$="/file:"+f$:gosub 160:rem post filename
   40 dopen#2,(f$)
   50 l$=""
   60 get#2,c$
   70 l$=l$+c$
   80 if len(l$)=128 then gosub 160:rem post chunk
   90 print n$;
  100 if not st and 64 then 60
  110 dclose#2
  120 if len(l$)<>0 then gosub 160:rem post remaining chunk
  130 gosub 160:rem post empty chunk to declare we've finished
  140 print "finished posting!"
  150 end
  160 rem *** post chunk ***
  170 l$=chr$(len(l$)+1) + l$:rem prepend the length of string+1
  180 a=usr("/"+l$)
  190 print "posting chunk..."
  200 l$=""
  210 return
