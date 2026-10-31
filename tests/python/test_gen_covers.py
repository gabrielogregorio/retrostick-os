import gen_covers
import pytest


@pytest.mark.parametrize('name, key', [
    ('Mock Hero, The (USA).sfc', 'mockhero'),
    ('Mock & Friends (Europe) [!].nes', 'mockandfriends'),
    ('mock_hero.gba', 'mockhero'),
])
def test_title_key_ignores_region_article_and_punctuation(name, key):
    assert gen_covers.title_key(name) == key


def test_the_same_game_gets_the_same_key_across_regions():
    assert gen_covers.title_key('Mock Hero (USA).nes') == gen_covers.title_key('Mock Hero (Japan) (Rev 1).nes')


def test_every_system_has_collection_folders():
    assert set(gen_covers.FOLDERS) <= set(gen_covers.SYSTEMS)
