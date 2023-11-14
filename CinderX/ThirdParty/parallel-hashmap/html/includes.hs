{-# LANGUAGE OverloadedStrings #-}

import Text.Read
import Control.Monad.State
import Control.Monad
import Text.Pandoc
import Data.Monoid
import Control.Applicative

import Text.Pandoc.JSON
import Text.Pandoc.Walk

slice :: Int -> Int -> [a] -> [a]
slice from to xs = take (to - from + 1) (drop from xs)

doSlice :: Block -> IO Block
doSlice cb@(CodeBlock (id, classes, namevals) contents) = do
  res <- return $ do
    upper <- readMaybe =<< lookup "upper" namevals
    lower <- readMaybe =<< lookup "lower" namevals
    file  <- lookup "slice" namevals
    return (upper, lower, file)

  case res of
    Nothing -> return cb
    Just (upper, lower, f) -> do
      contents <- readFile f
      let lns = unlines $ slice lower upper (lines contents)
      return (CodeBlock (id, classes, namevals) lns)
doSlice x = return x

doInclude :: Block -> IO Block
doInclude cb@(CodeBlock (id, classes, namevals) contents) =
  case lookup "include" namevals of
       Just f     -> return . (CodeBlock (id, classes, namevals)) =<< readFile f
       Nothing    -> return cb
doInclude x = return x

doHtml :: Block -> IO Block
doHtml cb@(CodeBlock (id, classes, namevals) contents) =
  case lookup "literal" namevals of
       Just f     -> return . (RawBlock "html") =<< readFile f
       Nothing    -> return cb
doHtml x = return x

injectLatexMacros :: Maybe Format -> Pandoc -> IO Pandoc
injectLatexMacros (Just fmt) p = do
  macros <- readFile "latex_macros"
  let block =
        case fmt of
          Format "html" ->
            Div ("",[],[("style","display:none")]) . (:[])
              . Para . (:[]) . Math DisplayMath $ macros
          Format "latex" -> RawBlock "latex" macros
          Format "epub" -> RawBlock "latex" macros
          _ -> RawBlock "latex" macros
  return (Pandoc nullMeta [block] <> p)
injectLatexMacros _ _ = return mempty

main :: IO ()
main = toJSONFilter
        ((\fmt -> injectLatexMacros fmt
         >=> walkM doInclude
         >=> walkM doSlice
         >=> walkM doHtml) :: Maybe Format -> Pandoc -> IO Pandoc)
