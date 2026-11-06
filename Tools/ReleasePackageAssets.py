"""Canonical, explicit text assets for portable release staging."""
from pathlib import Path
import shutil

from ReleaseChecksums import is_link


COMMON=(
    ('LICENSE.txt','LICENSE.txt'),
    ('app-settings.ini','settings/app-settings.ini'),
    ('fan-curves.csv','settings/fan-curves.csv'),
    ('licenses/AMD-ADL-MIT.txt','licenses/AMD-ADL-MIT.txt'),
)
PLATFORM={
    'windows':(
        ('windows/README.txt','README.txt'),
        ('windows/THIRD-PARTY-NOTICES.txt','THIRD-PARTY-NOTICES.txt'),
        ('windows/SOURCE_OFFER.txt','SOURCE_OFFER.txt'),
        ('windows/licenses/ADLX-SDK-LICENSE.txt','licenses/ADLX-SDK-LICENSE.txt'),
        ('windows/licenses/IGCL-SDK-LICENSE.txt','licenses/IGCL-SDK-LICENSE.txt'),
        ('windows/licenses/LGPL-2.1.txt','licenses/LGPL-2.1.txt'),
        ('windows/licenses/Insyde-H2OFFT/NOTICE.txt','licenses/Insyde-H2OFFT/NOTICE.txt'),
        ('windows/licenses/Insyde-H2OFFT/PROVENANCE.json','licenses/Insyde-H2OFFT/PROVENANCE.json'),
    ),
    'linux':(
        ('linux/README.txt','README.txt'),
        ('linux/THIRD-PARTY-NOTICES.txt','THIRD-PARTY-NOTICES.txt'),
        ('linux/start.sh','start.sh'),
        ('linux/libgtk4-layer-shell.LICENSE.txt','libgtk4-layer-shell.LICENSE.txt'),
        ('linux/licenses/DRM-UAPI-NOTICES.txt','licenses/DRM-UAPI-NOTICES.txt'),
    ),
}


def canonical_resources(root,platform):
    if platform not in PLATFORM:raise ValueError('unknown release platform')
    package=Path(root)/'packaging'
    if not package.is_dir() or is_link(package):raise ValueError('packaging directory unavailable or linked')
    resolved=package.resolve(strict=True)
    result=[]
    for source_name,target_name in COMMON+PLATFORM[platform]:
        source=package/source_name
        if not source.is_file() or is_link(source) or not source.resolve(strict=True).is_relative_to(resolved):
            raise ValueError('canonical release text missing or linked: '+str(source))
        data=source.read_bytes()
        if not data:raise ValueError('canonical release text is empty: '+str(source))
        data.decode('utf-8-sig')
        result.append((source,target_name))
    return result


def stage_canonical_text(root,platform,folder):
    resources=canonical_resources(root,platform)
    folder=Path(folder)
    if not folder.is_dir() or is_link(folder):raise ValueError('release staging directory unavailable or linked')
    resolved=folder.resolve(strict=True)
    for source,relative in resources:
        target=folder/relative
        if target.exists() or target.is_symlink():raise ValueError('refusing to overwrite staged text: '+str(target))
        if not target.parent.resolve().is_relative_to(resolved):raise ValueError('staged text escapes destination: '+str(target))
        parent=target.parent
        while parent!=folder:
            if parent.exists() and is_link(parent):raise ValueError('linked staging subdirectory: '+str(parent))
            parent=parent.parent
    for source,relative in resources:
        target=folder/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,target)
