# Copyright 2017-2019 (C) Calvin Rose

(print (string "Janet " janet/version "-" janet/build "  Copyright (C) 2017-2018 Calvin Rose"))

(fiber/new (fn webrepl []
  (repl (fn get-line [buf p]
          (def offset (parser/where p))
          (def prompt (string "janet:" offset ":" (parser/state p) "> "))
          (repl-yield prompt buf)
          (yield)
          buf))))
