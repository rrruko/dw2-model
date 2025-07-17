import Control.Applicative ((<|>))
import Data.List (intercalate)
import Data.Map.Strict (Map)
import qualified Data.Map.Strict as Map
import System.Environment (getArgs)
import Numeric (readDec, readHex, showHex)
import Data.Maybe (mapMaybe)

data IndexEntry
  = Animation Char Int String
  | Model Int String
  | Other Int String

data Filename
  = Mon Char Int String

-- Parse a filename of the format CNNNXXXX.EXT
-- where NNN is a decimal number
parseFilename :: String -> Maybe Filename
parseFilename filename =
  case filename of
    (f:name@(x:y:z:_)) ->
      case readDec [x,y,z] of
        [(n, "")] -> Just (Mon f n (take 7 name))
        _ -> Nothing
    _ -> Nothing

-- Parse an index entry of the form "* <offset> <filename>" where 'offset' is a
-- hexadecimal sector number
parseIndexEntry :: String -> Maybe IndexEntry
parseIndexEntry entry =
  case words entry of
    ["*", offsetStr, filename] ->
      case readHex offsetStr of
        [(offset, "")] -> 
          case parseFilename filename of
            Just (Mon c ix shortName) ->
              if c == 'M' && ix < 256 then
                Just (Model offset shortName)
              else if c >= 'A' && c <= 'M' && ix < 256 then
                Just (Animation c offset shortName)
              else
                Nothing
            Nothing ->
              Just (Other offset filename)
        _ -> Nothing 
    _ -> Nothing

data TableEntry
  = TableEntry
      { modelSector :: Maybe Int
      , animationSectors :: [(Char, Int)]
      }

instance Semigroup TableEntry where
  x <> y = 
    TableEntry 
      { modelSector = modelSector x <|> modelSector y
      , animationSectors = animationSectors x <> animationSectors y
      }

formatTableEntry :: String -> TableEntry -> Maybe String
formatTableEntry name (TableEntry modelSector animationSectors) = do
  ms <- modelSector
  pure (name ++ " " ++ showH ms ++ " " ++ intercalate " " (map showAnimationSector animationSectors))
  where
  showH s = showHex s ""
  showAnimationSector (label, sector) = showH sector ++ ":" ++ [label]

main :: IO ()
main = do
  args <- getArgs
  indexFile <- readFile (args !! 0)
  let fileEntries = mapMaybe parseIndexEntry (lines indexFile)
  let
    monsterMap :: Map String TableEntry
    monsterMap = foldr consumeIndexEntry Map.empty fileEntries

    consumeIndexEntry :: IndexEntry -> Map String TableEntry -> Map String TableEntry
    consumeIndexEntry entry m =
      case entry of
        Model sector name ->
          Map.insertWith
            (<>) 
            name
            (TableEntry (Just sector) [])
            m
        Animation c sector name ->
          Map.insertWith
            (<>) 
            name
            (TableEntry Nothing [(c, sector)])
            m 
        Other sector name ->
          m
    table :: [String]
    table = 
      Map.foldrWithKey
        (\k v acc ->
          case formatTableEntry k v of
            Just e -> e:acc
            Nothing -> acc)
        []
        monsterMap 
  putStrLn (unlines table)
